// wininput.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "linedisc.h"
#include "config.h"

#include <math.h>
#include <windowsx.h>

static HMENU menu, sysmenu;

void
win_update_menus(void)
{
  ModifyMenu(
    sysmenu, IDM_DUPLICATE, MF_BYCOMMAND | MF_STRING, IDM_DUPLICATE,
    cfg.window_shortcuts ? "&Duplicate\tAlt+F2" : "&Duplicate"
  );
  ModifyMenu(
    sysmenu, SC_CLOSE, MF_BYCOMMAND | MF_STRING, SC_CLOSE,
    cfg.window_shortcuts ? "&Close\tAlt+F4" : "&Close"
  ); 
  ModifyMenu(
    menu, IDM_COPY, MF_BYCOMMAND | MF_STRING, IDM_COPY,
    cfg.edit_shortcuts ? "&Copy\tCtrl+Ins" : "&Copy"
  );
  ModifyMenu(
    menu, IDM_PASTE, MF_BYCOMMAND | MF_STRING, IDM_PASTE,
    cfg.edit_shortcuts ? "&Paste\tShift+Ins" : "&Paste"
  );
  ModifyMenu(
    menu, IDM_DEFSIZE, MF_BYCOMMAND | MF_STRING, IDM_DEFSIZE,
    cfg.window_shortcuts ? "&Default size\tAlt+F10" : "&Default size"
  );
  ModifyMenu(
    menu, IDM_FULLSCREEN, MF_BYCOMMAND | MF_STRING, IDM_FULLSCREEN,
    cfg.window_shortcuts ? "&Fullscreen\tAlt+F11" : "&Fullscreen"
  );
  EnableMenuItem(menu, IDM_COPY, term.selected ? MF_ENABLED : MF_GRAYED);
  EnableMenuItem(
    menu, IDM_PASTE,
    IsClipboardFormatAvailable(CF_TEXT) || 
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED
  );
  CheckMenuItem(
    menu, IDM_FULLSCREEN,
    GetWindowLongPtr(wnd, GWL_STYLE) & WS_CAPTION
    ? MF_UNCHECKED : MF_CHECKED
  );
  EnableMenuItem(
    menu, IDM_DEFSIZE, 
    IsZoomed(wnd) || term.cols != cfg.cols || term.rows != cfg.rows
    ? MF_ENABLED : MF_GRAYED
  );
}

void
win_init_menus(void)
{
  menu = CreatePopupMenu();
  AppendMenu(menu, MF_ENABLED, IDM_COPY, 0);
  AppendMenu(menu, MF_ENABLED, IDM_PASTE, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_SELALL, "&Select All");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_RESET, "&Reset");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_DEFSIZE, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_OPTIONS, "&Options...");

  sysmenu = GetSystemMenu(wnd, false);
  InsertMenu(sysmenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, 0);
  InsertMenu(sysmenu, 0, MF_BYPOSITION | MF_ENABLED,
                         IDM_OPTIONS, "&Options...");
  InsertMenu(sysmenu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED, IDM_DUPLICATE, 0);
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

static enum {
  ALT_CANCELLED = -1, ALT_NONE = 0, ALT_ALONE = 1,
  ALT_OCT = 8, ALT_DEC = 10
} alt_state;
static wchar alt_char;

inline static bool
is_key_down(uchar vk)
{ return GetKeyState(vk) & 0x80; }

static mod_keys
get_mods(void)
{
  bool shift = is_key_down(VK_SHIFT);
  bool alt = is_key_down(VK_MENU);
  bool ctrl = is_key_down(VK_CONTROL);
  return shift * SHIFT | alt * ALT | ctrl * CTRL;
}

static void
update_mouse(mod_keys mods)
{
  static bool app_mouse;
  bool new_app_mouse = 
    term.mouse_mode &&
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
translate_pos(int x, int y)
{
  return (pos){
    .x = floorf((x - offset_width ) / (float)font_width ),
    .y = floorf((y - offset_height) / (float)font_height), 
  };
}

static pos
get_mouse_pos(LPARAM lp)
{
  return translate_pos(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));  
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
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;
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
  // WM_MOUSEWHEEL reports screen coordinates rather than client coordinates
  POINT wpos = {.x = GET_X_LPARAM(lp), .y = GET_Y_LPARAM(lp)};
  ScreenToClient(wnd, &wpos);
  pos tpos = translate_pos(wpos.x, wpos.y);

  int delta = GET_WHEEL_DELTA_WPARAM(wp);  // positive means up
  int lines_per_notch;
  SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_notch, 0);

  term_mouse_wheel(delta, lines_per_notch, get_mods(), tpos);
}


/* Keyboard handling */

bool 
win_key_down(WPARAM wp, LPARAM lp)
{
  uint key = wp;
  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  bool extended = HIWORD(lp) & KF_EXTENDED;
  uint count = LOWORD(lp);
  mod_keys mods = get_mods();
  bool shift = mods & SHIFT, alt = mods & ALT, ctrl = mods & CTRL;

  if (key == VK_PROCESSKEY) {
    TranslateMessage(
      &(MSG){.hwnd = wnd, .message = WM_KEYDOWN, .wParam = wp, .lParam = lp}
    );
    return 1;
  }
 
  update_mouse(mods);

  // Alt+keycode
  if (alt_state > ALT_NONE && VK_NUMPAD0 <= key && key <= VK_NUMPAD9) {
    int digit = key - VK_NUMPAD0;
    if (alt_state == ALT_ALONE) {
      alt_char = digit;
      alt_state = digit ? ALT_DEC : ALT_OCT;
      return 1;
    }
    else if (digit < alt_state) {
      alt_char *= alt_state;
      alt_char += digit;
      return 1;
    }
  }
  if (key == VK_MENU && !shift && !ctrl) {
    if (alt_state == ALT_NONE)
      alt_state = ALT_ALONE;
    return 1;
  }
  else if (alt_state != ALT_NONE)
    alt_state = ALT_CANCELLED;
  
  // Window commands
  if (alt && !ctrl && cfg.window_shortcuts) {
    WPARAM cmd;
    switch (key) {
      when VK_SPACE:  cmd = SC_KEYMENU;
      when VK_RETURN or VK_F11: cmd = IDM_FULLSCREEN;
      when VK_F2:     cmd = IDM_DUPLICATE;
      when VK_F4:     cmd = SC_CLOSE;
      when VK_F10:    cmd = IDM_DEFSIZE;
      otherwise: goto not_command;
    }
    SendMessage(wnd, WM_SYSCOMMAND, cmd, ' ');
    return 1;
  }
  not_command:
  
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
  if (key == VK_INSERT && cfg.edit_shortcuts) {
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
  if (term.which_screen == 0 && mods && mods == (mod_keys)cfg.scroll_mod) { 
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
  not_scroll:
  
  // Font zooming
  if (mods == CTRL && cfg.zoom_shortcuts && !term.modify_other_keys) {
    int zoom;
    switch (key) {
      when VK_OEM_PLUS or VK_ADD:       zoom = 1;
      when VK_OEM_MINUS or VK_SUBTRACT: zoom = -1;
      when '0' or VK_NUMPAD0:           zoom = 0;
      otherwise: goto not_zoom;
    }
    win_zoom_font(zoom);
    return 1;
  }
  not_zoom: ;
  
  // Keycode buffer.
  char buf[12];
  int  buf_len = 0;
  
  inline void ch(char c) { buf[buf_len++] = c; }
  void str(char *s) { while (*s) ch(*s++); }
  inline void esc(void) { ch('\e'); }
  inline void esc_if(bool b) { if (b) esc(); }  
  inline char C(char c) { return c & 0x1F; }
  void ss3(char c) { esc(); ch('O'); ch(c); }
  void csi(char c) { esc(); ch('['); ch(c); }
  void mod_csi(char c) { str("\e[1;"); ch(mods + '1'); ch(c); }
  void mod_other(wchar c) {
    buf_len = snprintf(buf, sizeof(buf), "\e[%u;%cu", c, mods + '1');
  }

  // Special keys.
  {
    switch (key) {
      when VK_RETURN:
        if (term.modify_other_keys && (shift || ctrl))
          mod_other('\r');
        else if (!ctrl)
          esc_if(alt),
          term.newline_mode ? str("\r\n") : ch(shift ? '\n' : '\r');
        else
          esc_if(shift || alt), ch(C('^'));
      when VK_BACK:
        if (!ctrl)
          esc_if(alt), ch(cfg.backspace_sends_del ? 0x7f : '\b');
        else if (!term.modify_other_keys)
          esc_if(shift || alt), ch(cfg.backspace_sends_del ? C('_') : 0x7f);
        else
          mod_other(cfg.backspace_sends_del ? 0x7f : '\b');
      when VK_TAB:
        if (!ctrl)
          shift ? csi('Z') : ch('\t');
        else if (!term.modify_other_keys)
          mod_csi('Z');
        else
          mod_other('\t');
      when VK_ESCAPE:
        ch(shift ? C(']') : cfg.escape_sends_fs ? C('\\') : C('['));
      when VK_PAUSE or VK_CANCEL:
        if (!ctrl && !alt)
          esc_if(shift), ch(key == VK_PAUSE ? C(']') : C('\\'));
        else
          return 0;
      otherwise: goto not_special;
    }
    goto send;
  }
  not_special:;
  
  bool letter = key == ' ' || ('A' <= key && key <= 'Z');
  bool modify_other =
    term.modify_other_keys &&
    (('0' <= key && key <= '9') || 
     (VK_OEM_1 <= key && key <= VK_OEM_102));
  bool skip_layout = ctrl && !alt && (letter || modify_other);
  
  uchar keyboard[256];  
  GetKeyboardState(keyboard);
  
  // Try keyboard layout.
  // ToUnicode produces up to four UTF-16 code units per keypress according
  // to an experiment with Keyboard Layout Creator 1.4. (MSDN doesn't say.)
  if (!skip_layout) { 
    wchar wbuf[4];
    int wbuf_len = ToUnicode(key, scancode, keyboard, wbuf, 4, 0);
    if (wbuf_len != 0) {
      // Got normal key or dead key.
      term_cancel_paste();
      term_seen_key_event();
      if (wbuf_len > 0) {
        bool meta = alt && !ctrl;
        do {
          if (meta)
            ldisc_send("\e", 1, 1);
          luni_send(wbuf, wbuf_len, 1);
        } while (--count);
      }
      hide_mouse();
      return 1;
    }
  }
  
  // xterm modifyOtherKeys mode (sends CSI u codes)
  if (modify_other) {
    keyboard[VK_CONTROL] = keyboard[VK_LCONTROL] = keyboard[VK_RCONTROL] = 0;
    wchar wc;
    int len = ToUnicode(key, scancode, keyboard, &wc, 1, 0);
    if (!len)
      return 0;
    mod_other(wc);
    if (len < 0) {
      // Nasty hack to clear dead key state, a la Michael Kaplan.
      memset(keyboard, 0, sizeof keyboard);
      scancode = MapVirtualKey(VK_DECIMAL, 0);
      while (ToUnicode(VK_DECIMAL, scancode, keyboard, &wc, 1, 0) < 0);
    }
    goto send;
  }
  
  // Ctrl+letter combinations
  if (ctrl && letter) {
    esc_if(alt || shift);
    ch(C(key));
    goto send;
  }
  
  char code;

  if (extended || !term.app_keypad) {
    // Cursor keys.
    switch (key) {
      when VK_UP:    code = 'A';
      when VK_DOWN:  code = 'B';
      when VK_RIGHT: code = 'C';
      when VK_LEFT:  code = 'D';
      when VK_CLEAR: code = 'E';
      when VK_HOME:  code = 'H';
      when VK_END:   code = 'F';
      when VK_BROWSER_BACK: code = 'G';
      when VK_BROWSER_FORWARD: code = 'I';
      otherwise: goto not_cursor;
    }
    mods ? mod_csi(code) : term.app_cursor_keys ? ss3(code) : csi(code);
    goto send;
    not_cursor:
    
    // Editing keys.
    switch (key) {
      when VK_PRIOR:  code = '5';
      when VK_NEXT:   code = '6';
      when VK_INSERT: code = '2';
      when VK_DELETE: code = '3';
      otherwise: goto not_edit;
    }
    esc(); ch('['); ch(code);
    if (mods) { ch(';'); ch('1' + mods); }
    ch('~');
    goto send;
    not_edit:;
  }
  
  // VT100-style application keypad
  {
    switch (key) {
      when VK_DELETE: code = '.';
      when VK_INSERT: code = '0';
      when VK_END:    code = '1';
      when VK_DOWN:   code = '2';
      when VK_NEXT:   code = '3';
      when VK_LEFT:   code = '4';
      when VK_CLEAR:  code = '5';
      when VK_RIGHT:  code = '6';
      when VK_HOME:   code = '7';
      when VK_UP:     code = '8';
      when VK_PRIOR:  code = '9';
      when '0' ... '9': code = key;
      when VK_NUMPAD0  ... VK_NUMPAD9:    code = key - VK_NUMPAD0  + '0';
      when VK_MULTIPLY ... VK_DIVIDE:     code = key - VK_MULTIPLY + '*';
      when VK_OEM_PLUS ... VK_OEM_PERIOD: code = key - VK_OEM_PLUS + '+';
      otherwise: goto not_app_keypad;
    }
    code += 'p' - '0';
    mods ? mod_csi(code) : ss3(code);
    goto send;
  }
  not_app_keypad:
  
  // PF keys.
  if (VK_F1 <= key && key <= VK_F4) {
    code = key - VK_F1 + 'P';
    mods ? mod_csi(code) : ss3(code);
    goto send;
  }
  
  // F keys.
  if (VK_F5 <= key && key <= VK_F24) {
    str("\e[");
    code = 
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
  
  return 0;
    
  // Send char buffer.
  send: {
    term_cancel_paste();
    term_seen_key_event();
    do
      ldisc_send(buf, buf_len, 1);
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
  if (alt) {
    if (alt_state == ALT_ALONE) {
      if (cfg.alt_sends_esc)
        ldisc_send("\e", 1, 1);
    }
    else if (alt_state > ALT_ALONE)
      luni_send(&alt_char, 1, 1);
    alt_state = ALT_NONE;
  }
  
  return alt;
}
