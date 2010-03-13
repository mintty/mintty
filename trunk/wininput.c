// wininput.c (part of mintty)
// Copyright 2008-10 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "linedisc.h"
#include "config.h"
#include "charset.h"

#include <math.h>
#include <windowsx.h>
#include <winnls.h>
#include <termios.h>

#if (WINVER < 0x0500)
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_102 0xE2
#endif

static HMENU menu, sysmenu;

void
win_update_menus(void)
{
  ModifyMenu(
    sysmenu, IDM_NEW, 0, IDM_NEW,
    term.shortcut_override ? "Ne&w" : "Ne&w\tAlt+F2" 
  );
  ModifyMenu(
    sysmenu, SC_CLOSE, 0, SC_CLOSE,
    term.shortcut_override ? "&Close" : "&Close\tAlt+F4" 
  ); 

  uint sel_enabled = term.selected ? MF_ENABLED : MF_GRAYED;
  EnableMenuItem(menu, IDM_OPEN, sel_enabled);
  ModifyMenu(
    menu, IDM_COPY, sel_enabled, IDM_COPY,
    term.shortcut_override ? "&Copy" : "&Copy\tCtrl+Ins"
  );

  uint paste_enabled =
    IsClipboardFormatAvailable(CF_TEXT) || 
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED;
  ModifyMenu(
    menu, IDM_PASTE, paste_enabled, IDM_PASTE,
    term.shortcut_override ? "&Paste" : "&Paste\tShift+Ins"
  );

  ModifyMenu(
    menu, IDM_RESET, 0, IDM_RESET,
    term.shortcut_override ?  "&Reset" : "&Reset\tAlt+F8"
  );

  uint defsize_enabled = 
    IsZoomed(wnd) || term.cols != cfg.cols || term.rows != cfg.rows
    ? MF_ENABLED : MF_GRAYED;
  ModifyMenu(
    menu, IDM_DEFSIZE, defsize_enabled, IDM_DEFSIZE,
    term.shortcut_override ? "&Default size" : "&Default size\tAlt+F10"
  );

  uint fullscreen_checked = win_is_fullscreen() ? MF_CHECKED : MF_UNCHECKED;
  ModifyMenu(
    menu, IDM_FULLSCREEN, fullscreen_checked, IDM_FULLSCREEN,
    term.shortcut_override ? "&Fullscreen" : "&Fullscreen\tAlt+F11"
  );

  uint options_enabled = config_wnd ? MF_GRAYED : MF_ENABLED;
  EnableMenuItem(menu, IDM_OPTIONS, options_enabled);
  EnableMenuItem(sysmenu, IDM_OPTIONS, options_enabled);
}

void
win_init_menus(void)
{
  menu = CreatePopupMenu();
  AppendMenu(menu, MF_ENABLED, IDM_OPEN, "Ope&n");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_COPY, 0);
  AppendMenu(menu, MF_ENABLED, IDM_PASTE, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_SELALL, "&Select All");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_RESET, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_DEFSIZE, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_OPTIONS, "&Options...");

  sysmenu = GetSystemMenu(wnd, false);
  InsertMenu(sysmenu, SC_CLOSE, MF_ENABLED, IDM_OPTIONS, "&Options...");
  InsertMenu(sysmenu, SC_CLOSE, MF_ENABLED, IDM_NEW, 0);
  InsertMenu(sysmenu, SC_CLOSE, MF_SEPARATOR, 0, 0);
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

typedef enum {
  ALT_CANCELLED = -1, ALT_NONE = 0, ALT_ALONE = 1,
  ALT_OCT = 8, ALT_DEC = 10
} alt_state_t;
static alt_state_t alt_state;
static wchar alt_char;

static bool lctrl;  // Is left Ctrl pressed?
static long lctrl_time;

static mod_keys
get_mods(void)
{
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
  lctrl_time = 0;
  lctrl &= is_key_down(VK_LCONTROL);
  return
    is_key_down(VK_SHIFT) * MDK_SHIFT |
    is_key_down(VK_MENU) * MDK_ALT |
    (lctrl | is_key_down(VK_RCONTROL)) * MDK_CTRL;
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

static void
hide_mouse()
{
  POINT p;
  if (mouse_showing && GetCursorPos(&p) && WindowFromPoint(p) == wnd) {
    ShowCursor(false);
    mouse_showing = false;
  }
}

static pos
translate_pos(int x, int y)
{
  return (pos){
    .x = floorf((x - PADDING) / (float)font_width ),
    .y = floorf((y - PADDING) / (float)font_height), 
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

void
win_mouse_move(bool nc, LPARAM lp)
{
  // Windows seems to like to occasionally send MOUSEMOVE events even if the
  // mouse hasn't moved. Don't do anything in this case.
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

static void
send_syscommand(WPARAM cmd)
{
  SendMessage(wnd, WM_SYSCOMMAND, cmd, ' ');
}

bool 
win_key_down(WPARAM wp, LPARAM lp)
{
  uint key = wp;

  if (key == VK_PROCESSKEY) {
    TranslateMessage(
      &(MSG){.hwnd = wnd, .message = WM_KEYDOWN, .wParam = wp, .lParam = lp}
    );
    return 1;
  }
 
  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  bool extended = HIWORD(lp) & KF_EXTENDED;
  uint count = LOWORD(lp);

  uchar kbd[256];
  GetKeyboardState(kbd);
  inline bool is_key_down(uchar vk) { return kbd[vk] & 0x80; }
  
  // Distinguish real LeftCtrl from keypresses from messages sent for AltGr.
  if (key == VK_CONTROL && !extended) {
    lctrl = true;
    lctrl_time = GetMessageTime();
  }
  else if (lctrl_time) {
    lctrl = !(key == VK_MENU && extended && lctrl_time == GetMessageTime());
    lctrl_time = 0;
  }
  else
    lctrl &= is_key_down(VK_LCONTROL);

  bool
    numlock = kbd[VK_NUMLOCK] & 1,
    shift = is_key_down(VK_SHIFT),
    lalt = is_key_down(VK_LMENU),
    ralt = is_key_down(VK_RMENU),
    alt = lalt | ralt,
    rctrl = is_key_down(VK_RCONTROL),
    ctrl = lctrl | rctrl,
    ctrl_lalt_altgr = cfg.ctrl_alt_is_altgr & ctrl & lalt & !ralt,
    altgr = ralt | ctrl_lalt_altgr;

  mod_keys mods = shift * MDK_SHIFT | alt * MDK_ALT | ctrl * MDK_CTRL;

  update_mouse(mods);

  alt_state_t old_alt_state = alt_state;
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;
  
  // Context and window menus
  if (key == VK_APPS) {
    if (shift)
      send_syscommand(SC_KEYMENU);
    else {
      win_show_mouse();
      POINT p;
      GetCaretPos(&p);
      ClientToScreen(wnd, &p);
      TrackPopupMenu(
        menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        p.x, p.y, 0, wnd, null
      );
    }
    return 1;
  }
  
  if (!term.shortcut_override) {

    // Window menu and fullscreen
    if (cfg.window_shortcuts && alt && !ctrl) {
      if (key == VK_RETURN) {
        send_syscommand(IDM_FULLSCREEN);
        return 1;
      }
      else if (key == VK_SPACE) {
        send_syscommand(SC_KEYMENU);
        return 1;
      }
    }

    // Alt+Fn shortcuts
    if (alt && VK_F1 <= key && key <= VK_F24) {
      if (mods == MDK_ALT) {
        WPARAM cmd;
        switch (key) {
          when VK_F2:  cmd = IDM_NEW;
          when VK_F4:  cmd = SC_CLOSE;
          when VK_F8:  cmd = IDM_RESET;
          when VK_F10: cmd = IDM_DEFSIZE;
          when VK_F11: cmd = IDM_FULLSCREEN;
          otherwise: return 1;
        }
        send_syscommand(cmd);
      }
      return 1;
    }
    
    // Font zooming
    if (cfg.zoom_shortcuts && mods == MDK_CTRL) {
      int zoom;
      switch (key) {
        when VK_OEM_PLUS or VK_ADD:       zoom = 1;
        when VK_OEM_MINUS or VK_SUBTRACT: zoom = -1;
        when '0' or VK_NUMPAD0:           zoom = 0;
        otherwise: goto not_zoom;
      }
      win_zoom_font(zoom);
      return 1;
      not_zoom:;
    }
    
    // Scrollback
    if (mods && mods == (mod_keys)cfg.scroll_mod &&
        (term.which_screen == 0 || cfg.alt_screen_scroll)) {
      WPARAM scroll;
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
      not_scroll:;
    }
    
    // Copy&paste
    if (key == VK_INSERT) {
      if (mods == MDK_CTRL) { term_copy(); return 1; }
      if (mods == MDK_SHIFT) { win_paste(); return 1; }
    }
  }
  
  // Keycode buffers
  char buf[12];
  wchar wbuf[8];
  int len = 0, wlen = 0;

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
    len = sprintf(buf, "\e[%u;%cu", c, mods + '1');
  }
  void app_pad_code(char c) { mod_ss3(c - '0' + 'p'); }

  bool alt_code_key(char digit) {
    if (old_alt_state == ALT_ALONE) {
      alt_char = digit;
      alt_state = digit ? ALT_DEC : ALT_OCT;
      return true;
    }
    if (old_alt_state > ALT_ALONE && digit < old_alt_state) {
      alt_state = old_alt_state;
      alt_char *= alt_state;
      alt_char += digit;
      return true;
    }
    return false;
  }

  bool app_pad_key(char symbol) {
    if (extended)
      return false;
    if (term.app_keypad && !term.app_cursor_keys) {
      // If NumLock is on, Shift must have been pressed to override it and
      // get a VK code for an editing or cursor key code.
      mods |= numlock;
      app_pad_code(symbol);
      return true;
    }
    return symbol != '.' && alt_code_key(symbol - '0');
  }
  
  void edit_key(uchar code, char symbol) {
    if (!app_pad_key(symbol))
      tilde_code(code);
  }
  
  void cursor_key(char code, char symbol) {
    if (!app_pad_key(symbol))
      mods ? mod_csi(code) : term.app_cursor_keys ? ss3(code) : csi(code);
  }

  // Keyboard layout
  bool layout(void) {
    wlen = ToUnicode(key, scancode, kbd, wbuf, lengthof(wbuf), 0);
    if (!wlen)
      return 0;
    if (wlen > 0)
      esc_if(alt);
    else
      wlen = 0;
    return 1;
  }
  
  wchar undead_keycode(void) {
    wchar wc;
    int len = ToUnicode(key, scancode, kbd, &wc, 1, 0);
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
    kbd[VK_CONTROL] = 0;
    wchar wc = undead_keycode();
    if (wc)
      other_code(wc);
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
  
  void ctrl_ch(uchar c) {
    esc_if(alt);
    if (!shift)
      ch(c);
    else {
      // Send C1 control char if the charset supports it.
      // Otherwise prefix the C0 char with ESC.
      wchar wc = c | 0x80;
      int l = cs_wcntombn(buf + len, &wc, cs_cur_max, 1);
      if (l > 0 && buf[len] != '?')
        len += l;
      else
        buf[0] = '\e', buf[1] = c, len = 2;
    }
  }
  
  bool ctrl_key(void) {
    bool try_key(void) {
      wchar wc = undead_keycode();
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
      shift = false;
      if (try_key())
        return true;
      shift = is_key_down(VK_SHIFT);
      kbd[VK_SHIFT] ^= 0x80;
      if (try_key())
        return true;
      kbd[VK_SHIFT] ^= 0x80;
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
  
  switch(key) {
    when VK_MENU:
      if (!shift && !is_key_down(VK_CONTROL))
        alt_state = old_alt_state == ALT_NONE ? ALT_ALONE : old_alt_state;
      return 1;
    when VK_RETURN:
      if (extended && !numlock && term.app_keypad)
        mod_ss3('M');
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
      if (alt)
        return 0;
      if (!ctrl)
        shift ? csi('Z') : ch('\t');
      else
        term.modify_other_keys ? other_code('\t') : mod_csi('I');
    when VK_ESCAPE:
      term.app_escape_key
      ? ss3('[')
      : ctrl_ch(term.escape_sends_fs ? CTRL('\\') : CTRL('['));
    when VK_PAUSE:
      ctrl_ch(ctrl & !extended ? CTRL('\\') : CTRL(']'));
    when VK_CANCEL:
      ctrl_ch(CTRL('\\'));
    when VK_F1 ... VK_F4:
      mod_ss3(key - VK_F1 + 'P');
    when VK_F5 ... VK_F24:
      tilde_code(
        (uchar[]){
          15, 17, 18, 19, 20, 21, 23, 24, 25, 26,
          28, 29, 31, 32, 33, 34, 36, 37, 38, 39
        }[key - VK_F5]
      );
    when VK_INSERT: edit_key(2, '0');
    when VK_DELETE: edit_key(3, '.');
    when VK_PRIOR:  edit_key(5, '9');
    when VK_NEXT:   edit_key(6, '3');
    when VK_HOME:   cursor_key('H', '7');
    when VK_END:    cursor_key('F', '1');
    when VK_UP:     cursor_key('A', '8');
    when VK_DOWN:   cursor_key('B', '2');
    when VK_LEFT:   cursor_key('D', '4');
    when VK_RIGHT:  cursor_key('C', '6');
    when VK_CLEAR:  cursor_key('E', '5');
    when VK_MULTIPLY ... VK_DIVIDE:
      if (mods || (term.app_keypad && !numlock) || !layout())
        app_pad_code(key - VK_MULTIPLY + '*');
    when VK_NUMPAD0 ... VK_NUMPAD9:
        !(term.app_keypad && !term.app_cursor_keys) 
        && alt_code_key(key - VK_NUMPAD0)
        ?: layout()
        ?: app_pad_code(key - VK_NUMPAD0 + '0');
    when 'A' ... 'Z' or ' ':
      if (char_key())
        break;
      if (term.modify_other_keys > 1)
        modify_other_key();
      else if (!ctrl_key())
        ctrl_ch(CTRL(key));
    when '0' ... '9' or VK_OEM_1 ... VK_OEM_102:
      if (char_key())
        break;
      if (term.modify_other_keys)
        modify_other_key();
      else if (!ctrl_key()) {
        // Treat remaining digits and symbols as apppad combinations
        switch (key) {
          when '0' ... '9': app_pad_code(key);
          when VK_OEM_PLUS ... VK_OEM_PERIOD:
            app_pad_code(key - VK_OEM_PLUS + '+');
        }
      }
    otherwise: return 0;
  }
  
  hide_mouse();
  term_cancel_paste();
  term_seen_key_event();

  do {
    if (len) ldisc_send(buf, len, 1);
    if (wlen) luni_send(wbuf, wlen, 1);
  } while (--count);

  return 1;
}

bool
win_key_up(WPARAM wp, LPARAM unused(lp))
{
  win_update_mouse();

  if (wp != VK_MENU)
    return false;

  if (alt_state == ALT_ALONE) {
    if (cfg.alt_sends_esc)
      term.app_escape_key ? ldisc_send("\eO[", 3, 1) : ldisc_send("\e", 1, 1);
  }
  else if (alt_state > ALT_ALONE) {
    if (cs_cur_max == 1)
      ldisc_send((char[]){alt_char}, 1, 1);
    else {
      if (alt_char < 0x20)
        MultiByteToWideChar(CP_OEMCP, MB_USEGLYPHCHARS,
                            (char[]){alt_char}, 1, &alt_char, 1);
      luni_send(&alt_char, 1, 1);
    }
  }
  
  alt_state = ALT_NONE;
  return true;
}
