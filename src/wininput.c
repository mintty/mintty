// wininput.c (part of mintty)
// Copyright 2008-12 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"

#include "charset.h"
#include "child.h"

#include <math.h>
#include <windowsx.h>
#include <winnls.h>
#include <termios.h>

static HMENU menu, sysmenu;
static bool alt_F2_pending = false;
static bool alt_F2_shifted = false;
static bool alt_F2_home = false;
static int alt_F2_monix = 0, alt_F2_moniy = 0;

void
win_update_menus(void)
{
  bool shorts = !term.shortcut_override;
  bool clip = shorts && cfg.clip_shortcuts;
  bool alt_fn = shorts && cfg.alt_fn_shortcuts;
  bool ct_sh = shorts && cfg.ctrl_shift_shortcuts;

  ModifyMenu(
    sysmenu, IDM_NEW, 0, IDM_NEW,
    alt_fn ? "Ne&w\tAlt+F2" : ct_sh ? "Ne&w\tCtrl+Shift+N" : "Ne&w"
  );
  ModifyMenu(
    sysmenu, SC_CLOSE, 0, SC_CLOSE,
    alt_fn ? "&Close\tAlt+F4" : ct_sh ? "&Close\tCtrl+Shift+W" : "&Close"
  );

  uint sel_enabled = term.selected ? MF_ENABLED : MF_GRAYED;
  EnableMenuItem(menu, IDM_OPEN, sel_enabled);
  ModifyMenu(
    menu, IDM_COPY, sel_enabled, IDM_COPY,
    clip ? "&Copy\tCtrl+Ins" : ct_sh ? "&Copy\tCtrl+Shift+C" : "&Copy"
  );

  uint paste_enabled =
    IsClipboardFormatAvailable(CF_TEXT) ||
    IsClipboardFormatAvailable(CF_UNICODETEXT) ||
    IsClipboardFormatAvailable(CF_HDROP)
    ? MF_ENABLED : MF_GRAYED;
  ModifyMenu(
    menu, IDM_PASTE, paste_enabled, IDM_PASTE,
    clip ? "&Paste\tShift+Ins" : ct_sh ? "&Paste\tCtrl+Shift+V" : "&Paste"
  );

  ModifyMenu(
    menu, IDM_SEARCH, 0, IDM_SEARCH,
    alt_fn ? "S&earch\tAlt+F3" : ct_sh ? "S&earch\tCtrl+Shift+H" : "S&earch"
  );

  ModifyMenu(
    menu, IDM_RESET, 0, IDM_RESET,
    alt_fn ? "&Reset\tAlt+F8" : ct_sh ? "&Reset\tCtrl+Shift+R" : "&Reset"
  );

  uint defsize_enabled =
    IsZoomed(wnd) || term.cols != cfg.cols || term.rows != cfg.rows
    ? MF_ENABLED : MF_GRAYED;
  ModifyMenu(
    menu, IDM_DEFSIZE_ZOOM, defsize_enabled, IDM_DEFSIZE_ZOOM,
    alt_fn ? "&Default size\tAlt+F10" :
    ct_sh ? "&Default size\tCtrl+Shift+D" : "&Default size"
  );

  uint fullscreen_checked = win_is_fullscreen ? MF_CHECKED : MF_UNCHECKED;
  ModifyMenu(
    menu, IDM_FULLSCREEN_ZOOM, fullscreen_checked, IDM_FULLSCREEN_ZOOM,
    alt_fn ? "&Full Screen\tAlt+F11" :
    ct_sh ? "&Full Screen\tCtrl+Shift+F" : "&Full Screen"
  );

  uint otherscreen_checked = term.show_other_screen ? MF_CHECKED : MF_UNCHECKED;
  ModifyMenu(
    menu, IDM_FLIPSCREEN, otherscreen_checked, IDM_FLIPSCREEN,
    alt_fn ? "Flip &Screen\tAlt+F12" :
    ct_sh ? "Flip &Screen\tCtrl+Shift+S" : "Flip &Screen"
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
  AppendMenu(menu, MF_ENABLED, IDM_SELALL, "Select &All");
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_SEARCH, 0);
  AppendMenu(menu, MF_ENABLED, IDM_RESET, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_DEFSIZE_ZOOM, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_FULLSCREEN_ZOOM, 0);
  AppendMenu(menu, MF_ENABLED | MF_UNCHECKED, IDM_FLIPSCREEN, 0);
  AppendMenu(menu, MF_SEPARATOR, 0, 0);
  AppendMenu(menu, MF_ENABLED, IDM_OPTIONS, "&Options...");

  sysmenu = GetSystemMenu(wnd, false);
  InsertMenu(sysmenu, SC_CLOSE, MF_ENABLED, IDM_COPYTITLE, "Copy &Title");
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
  ALT_OCT = 8, ALT_DEC = 10, ALT_HEX = 16
} alt_state_t;
static alt_state_t alt_state;
static uint alt_code;

static bool lctrl;  // Is left Ctrl pressed?
static int lctrl_time;

static mod_keys
get_mods(void)
{
  inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
  lctrl_time = 0;
  lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));
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
    term.mouse_mode && !term.show_other_screen &&
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
  if (cfg.hide_mouse && mouse_showing && GetCursorPos(&p) && WindowFromPoint(p) == wnd) {
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

static LPARAM last_lp = -1;
static pos last_pos = {-1, -1};

static pos
get_mouse_pos(LPARAM lp)
{
  last_lp = lp;
  return translate_pos(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
}

void
win_mouse_click(mouse_button b, LPARAM lp)
{
  static mouse_button last_button;
  static uint last_time, count;
  static pos last_click_pos;

  win_show_mouse();
  mod_keys mods = get_mods();
  pos p = get_mouse_pos(lp);

  uint t = GetMessageTime();
  if (b != last_button ||
      p.x != last_click_pos.x || p.y != last_click_pos.y ||
      t - last_time > GetDoubleClickTime() || ++count > 3)
    count = 1;

  SetFocus(wnd);  // in case focus was in search bar
  term_mouse_click(b, mods, p, count);
  last_pos = (pos){INT_MIN, INT_MIN};
  last_click_pos = p;
  last_time = t;
  last_button = b;
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;
}

void
win_mouse_release(mouse_button b, LPARAM lp)
{
  term_mouse_release(b, get_mods(), get_mouse_pos(lp));
  ReleaseCapture();
}

void
win_mouse_move(bool nc, LPARAM lp)
{
  if (lp == last_lp)
    return;

  win_show_mouse();

  pos p = get_mouse_pos(lp);
  if (nc || (p.x == last_pos.x && p.y == last_pos.y))
    return;

  last_pos = p;
  term_mouse_move(get_mods(), p);
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
send_syscommand2(WPARAM cmd, LPARAM p)
{
  SendMessage(wnd, WM_SYSCOMMAND, cmd, p);
}

static void
send_syscommand(WPARAM cmd)
{
  SendMessage(wnd, WM_SYSCOMMAND, cmd, ' ');
}

#define dont_debug_virtual_key_codes

#ifdef debug_virtual_key_codes
static struct {
  uint vk_;
  char * vk_name;
} vk_names[] = {
#include "vk_names.t"
};

static string
vk_name(uint key)
{
  for (uint i = 0; i < lengthof(vk_names); i++)
    if (key == vk_names[i].vk_)
      return vk_names[i].vk_name;
  static char vk_name[3];
  sprintf(vk_name, "%02X", key & 0xFF);
  return vk_name;
}
#endif

void
win_key_reset()
{
  alt_state = ALT_NONE;
}

#define dont_debug_key

#ifdef debug_key
#define trace_key(tag)	printf(" <-%s\n", tag)
#else
#define trace_key(tag)	
#endif

bool
win_key_down(WPARAM wp, LPARAM lp)
{
  uint key = wp;

#ifdef debug_virtual_key_codes
  printf("win_key_down %s\n", vk_name(key));
#endif

  if (key == VK_PROCESSKEY) {
    TranslateMessage(
      &(MSG){.hwnd = wnd, .message = WM_KEYDOWN, .wParam = wp, .lParam = lp}
    );
    return 1;
  }

  uint scancode = HIWORD(lp) & (KF_EXTENDED | 0xFF);
  bool extended = HIWORD(lp) & KF_EXTENDED;
  bool repeat = HIWORD(lp) & KF_REPEAT;
  uint count = LOWORD(lp);

  uchar kbd[256];
  GetKeyboardState(kbd);
  inline bool is_key_down(uchar vk) { return kbd[vk] & 0x80; }

  // Distinguish real LCONTROL keypresses from fake messages sent for AltGr.
  // It's a fake if the next message is an RMENU with the same timestamp.
  if (key == VK_CONTROL && !extended) {
    lctrl = true;
    lctrl_time = GetMessageTime();
  }
  else if (lctrl_time) {
    lctrl = !(key == VK_MENU && extended && lctrl_time == GetMessageTime());
    lctrl_time = 0;
  }
  else
    lctrl = is_key_down(VK_LCONTROL) && (lctrl || !is_key_down(VK_RMENU));

  bool numlock = kbd[VK_NUMLOCK] & 1;
  bool shift = is_key_down(VK_SHIFT);
  bool lalt = is_key_down(VK_LMENU);
  bool ralt = is_key_down(VK_RMENU);
  bool alt = lalt | ralt;
  bool rctrl = is_key_down(VK_RCONTROL);
  bool ctrl = lctrl | rctrl;
  bool ctrl_lalt_altgr = cfg.ctrl_alt_is_altgr & ctrl & lalt & !ralt;
  bool altgr = ralt | ctrl_lalt_altgr;

  mod_keys mods = shift * MDK_SHIFT | alt * MDK_ALT | ctrl * MDK_CTRL;

  update_mouse(mods);

  if (key == VK_MENU) {
    if (!repeat && mods == MDK_ALT && alt_state == ALT_NONE)
      alt_state = ALT_ALONE;
    return 1;
  }

  alt_state_t old_alt_state = alt_state;
  if (alt_state > ALT_NONE)
    alt_state = ALT_CANCELLED;

  // Context and window menus
  if (key == VK_APPS && !*cfg.key_menu) {
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

  // Exit when pressing Enter or Escape while holding the window open after
  // the child process has died.
  if ((key == VK_RETURN || key == VK_ESCAPE) && !mods && !child_is_alive())
    exit_mintty();

  if (alt_F2_pending) {
    if (!extended) {  // only accept numeric keypad
      switch (key) {
        when VK_HOME : alt_F2_monix--; alt_F2_moniy--;
        when VK_UP   : alt_F2_moniy--;
        when VK_PRIOR: alt_F2_monix++; alt_F2_moniy--;
        when VK_LEFT : alt_F2_monix--;
        when VK_CLEAR: alt_F2_monix = 0; alt_F2_moniy = 0; alt_F2_home = true;
        when VK_RIGHT: alt_F2_monix++;
        when VK_END  : alt_F2_monix--; alt_F2_moniy++;
        when VK_DOWN : alt_F2_moniy++;
        when VK_NEXT : alt_F2_monix++; alt_F2_moniy++;
        when VK_INSERT or VK_DELETE:
                       alt_F2_monix = 0; alt_F2_moniy = 0; alt_F2_home = false;
      }
    }
    return 1;
  }

  if (!term.shortcut_override) {

    // Copy&paste
    if (cfg.clip_shortcuts && key == VK_INSERT && mods && !alt) {
      if (ctrl)
        term_copy();
      if (shift)
        win_paste();
      return 1;
    }

#ifdef check_alt_ret_space_first
    // Moved to switch() below so we can override it with layout().
    // Window menu and fullscreen
    if (cfg.window_shortcuts && alt && !ctrl) {
      if (key == VK_RETURN) {
        trace_resize (("--- Alt-Enter (shift %d)", shift));
        send_syscommand(IDM_FULLSCREEN_ZOOM);
        return 1;
      }
      else if (key == VK_SPACE) {
        send_syscommand(SC_KEYMENU);
        return 1;
      }
    }
#endif

    // Alt+Fn shortcuts
    if (cfg.alt_fn_shortcuts && alt && VK_F1 <= key && key <= VK_F24) {
      if (!ctrl) {
        switch (key) {
          when VK_F2:
            // defer send_syscommand(IDM_NEW) until key released
            // monitor cursor keys to collect parameters meanwhile
            alt_F2_pending = true;
            alt_F2_home = false; alt_F2_monix = 0; alt_F2_moniy = 0;
            if (mods & MDK_SHIFT)
              alt_F2_shifted = true;
            else
              alt_F2_shifted = false;
          when VK_F3:  send_syscommand(IDM_SEARCH);
          when VK_F4:  send_syscommand(SC_CLOSE);
          when VK_F8:  send_syscommand(IDM_RESET);
          when VK_F10: send_syscommand(IDM_DEFSIZE_ZOOM);
          when VK_F11: send_syscommand(IDM_FULLSCREEN_ZOOM);
          when VK_F12: send_syscommand(IDM_FLIPSCREEN);
        }
      }
      return 1;
    }

    // Ctrl+Shift+letter shortcuts
    if (cfg.ctrl_shift_shortcuts && 'A' <= key && key <= 'Z' &&
        mods == (cfg.ctrl_exchange_shift ? MDK_CTRL : (MDK_CTRL | MDK_SHIFT))
       ) {
      switch (key) {
        when 'A': term_select_all();
        when 'C': term_copy();
        when 'V': win_paste();
        when 'N': send_syscommand(IDM_NEW);
        when 'W': send_syscommand(SC_CLOSE);
        when 'R': send_syscommand(IDM_RESET);
        when 'D': send_syscommand(IDM_DEFSIZE);
        when 'F': send_syscommand(IDM_FULLSCREEN);
        when 'S': send_syscommand(IDM_FLIPSCREEN);
        when 'H': send_syscommand(IDM_SEARCH);
      }
      return 1;
    }

    // Scrollback
    if (!term.on_alt_screen || term.show_other_screen) {
      mod_keys scroll_mod = cfg.scroll_mod ?: 8;
      if (cfg.pgupdn_scroll && (key == VK_PRIOR || key == VK_NEXT) &&
          !(mods & ~scroll_mod))
        mods ^= scroll_mod;
      if (mods == scroll_mod) {
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
    }

    // Font zooming
    if (cfg.zoom_shortcuts && (mods & ~MDK_SHIFT) == MDK_CTRL) {
      int zoom;
      switch (key) {
        // numeric keypad keys:
        // -- handle these ahead, i.e. here
        when VK_SUBTRACT:  zoom = -1;
        when VK_ADD:       zoom = 1;
        when VK_NUMPAD0:   zoom = 0;
          // Shift+VK_NUMPAD0 would be VK_INSERT but don't mangle that!
        // normal keys:
        // -- handle these in the course of layout() and other checking,
        // -- see below
        //when VK_OEM_MINUS: zoom = -1; mods &= ~MDK_SHIFT;
        //when VK_OEM_PLUS:  zoom = 1; mods &= ~MDK_SHIFT;
        //when '0':          zoom = 0;
        otherwise: goto not_zoom;
      }
      win_zoom_font(zoom, mods & MDK_SHIFT);
      return 1;
      not_zoom:;
    }
  }

  bool zoom_hotkey(void) {
    if (!term.shortcut_override && cfg.zoom_shortcuts
        && (mods & ~MDK_SHIFT) == MDK_CTRL) {
      int zoom;
      switch (key) {
        // numeric keypad keys:
        // -- handle these ahead, see above
        //when VK_SUBTRACT:  zoom = -1;
        //when VK_ADD:       zoom = 1;
        //when VK_NUMPAD0:   zoom = 0;
          // Shift+VK_NUMPAD0 would be VK_INSERT but don't mangle that!
        // normal keys:
        // -- handle these in the course of layout() and other checking,
        // -- so handle the case that something is assigned to them
        // -- e.g. Ctrl+Shift+- -> Ctrl+_
        // -- or even a custom Ctrl+- mapping
        // depending on keyboard layout, these may already be shifted!
        // thus better ignore the shift state, at least for -/+
        when VK_OEM_MINUS: zoom = -1; mods &= ~MDK_SHIFT;
        when VK_OEM_PLUS:  zoom = 1; mods &= ~MDK_SHIFT;
        when '0':          zoom = 0;
        otherwise: return 0;
      }
      win_zoom_font(zoom, mods & MDK_SHIFT);
      return 1;
    }
    return 0;
  }

  // Keycode buffers
  char buf[32];
  int len = 0;

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
#ifdef support_alt_meta_combinations
    // not too useful as mintty doesn't support Alt even with F-keys at all
    if (altgr && is_key_down(VK_LMENU))
      len = sprintf(buf, "\e[%u;%du", c, mods + 9);
    else
#endif
    len = sprintf(buf, "\e[%u;%cu", c, mods + '1');
  }
  void app_pad_code(char c) {
    void mod_appl_xterm(char c) {len = sprintf(buf, "\eO%c%c", mods + '1', c);}
    if (mods && term.app_keypad) switch (key) {
      when VK_DIVIDE or VK_MULTIPLY or VK_SUBTRACT or VK_ADD or VK_RETURN:
        mod_appl_xterm(c - '0' + 'p');
        return;
    }
    if (term.vt220_keys && mods && term.app_keypad) switch (key) {
      when VK_CLEAR or VK_PRIOR ... VK_DOWN or VK_INSERT or VK_DELETE:
        mod_appl_xterm(c - '0' + 'p');
        return;
    }
    mod_ss3(c - '0' + 'p');
  }
  void strcode(string s) {
    unsigned int code;
    if (sscanf (s, "%u", & code) == 1)
      tilde_code(code);
    else
      len = sprintf(buf, "%s", s);
  }

  bool alt_code_key(char digit) {
    if (old_alt_state > ALT_ALONE && digit < old_alt_state) {
      alt_state = old_alt_state;
      alt_code = alt_code * alt_state + digit;
      return true;
    }
    return false;
  }

  bool alt_code_numpad_key(char digit) {
    if (old_alt_state == ALT_ALONE) {
      alt_code = digit;
      alt_state = digit ? ALT_DEC : ALT_OCT;
      return true;
    }
    return alt_code_key(digit);
  }

  bool app_pad_key(char symbol) {
    if (extended)
      return false;
    // Mintty-specific: produce app_pad codes not only when vt220 mode is on,
    // but also in PC-style mode when app_cursor_keys is off, to allow the
    // numpad keys to be distinguished from the cursor/editing keys.
    if (term.app_keypad && (!term.app_cursor_keys || term.vt220_keys)) {
      // If NumLock is on, Shift must have been pressed to override it and
      // get a VK code for an editing or cursor key code.
      if (numlock)
        mods |= MDK_SHIFT;
      app_pad_code(symbol);
      return true;
    }
    return symbol != '.' && alt_code_numpad_key(symbol - '0');
  }

  void edit_key(uchar code, char symbol) {
    if (!app_pad_key(symbol)) {
      if (code != 3 || ctrl || alt || shift || !term.delete_sends_del)
        tilde_code(code);
      else
        ch(CDEL);
    }
  }

  void cursor_key(char code, char symbol) {
    if (!app_pad_key(symbol))
      mods ? mod_csi(code) : term.app_cursor_keys ? ss3(code) : csi(code);
  }

static struct {
  unsigned int combined;
  unsigned int base;
  unsigned int spacing;
} comb_subst[] = {
#include "combined.t"
};

  // Keyboard layout
  bool layout(void) {
    // ToUnicode returns up to 4 wchars according to
    // http://blogs.msdn.com/b/michkap/archive/2006/03/24/559169.aspx
    // https://web.archive.org/web/20120103012712/http://blogs.msdn.com/b/michkap/archive/2006/03/24/559169.aspx
    wchar wbuf[4];
    int wlen = ToUnicode(key, scancode, kbd, wbuf, lengthof(wbuf), 0);
    if (!wlen)     // Unassigned.
      return false;
    if (wlen < 0)  // Dead key.
      return true;

    esc_if(alt);

    // Substitute accent compositions not supported by Windows
    if (wlen == 2)
      for (unsigned int i = 0; i < lengthof(comb_subst); i++)
        if (comb_subst[i].spacing == wbuf[0] && comb_subst[i].base == wbuf[1]
            && comb_subst[i].combined < 0xFFFF  // -> wchar/UTF-16: BMP only
           ) {
          wchar wtmp = comb_subst[i].combined;
          short mblen = cs_wcntombn(buf + len, &wtmp, lengthof(buf) - len, 1);
          // short to recognise 0xFFFD as negative (WideCharToMultiByte...?)
          if (mblen > 0) {
            wbuf[0] = comb_subst[i].combined;
            wlen = 1;
          }
          break;
        }

    // Check that the keycode can be converted to the current charset
    // before returning success.
    int mblen = cs_wcntombn(buf + len, wbuf, lengthof(buf) - len, wlen);
#ifdef debug_ToUnicode
    printf("wlen %d:", wlen);
    for (int i = 0; i < wlen; i ++) printf(" %04X", wbuf[i] & 0xFFFF);
    printf("\n");
    printf("mblen %d:", mblen);
    for (int i = 0; i < mblen; i ++) printf(" %02X", buf[i] & 0xFF);
    printf("\n");
#endif
    bool ok = mblen > 0;
    len = ok ? len + mblen : 0;
    return ok;
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
    wchar wc = undead_keycode();
    if (!wc) {
      if (mods & MDK_SHIFT) {
        kbd[VK_SHIFT] = 0;
        wc = undead_keycode();
      }
    }
    if (wc) {
      if (altgr && !is_key_down(VK_LMENU))
        mods &= ~ MDK_ALT;
      other_code(wc);
    }
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

  bool altgr_key(void) {
    if (!altgr)
      return false;

    alt = lalt & !ctrl_lalt_altgr;

    // Sync keyboard layout with our idea of AltGr.
    kbd[VK_CONTROL] = altgr ? 0x80 : 0;

    // Don't handle Ctrl combinations here.
    // Need to check there's a Ctrl that isn't part of Ctrl+LeftAlt==AltGr.
    if ((ctrl & !ctrl_lalt_altgr) | (lctrl & rctrl))
      return false;

    // Try the layout.
    return layout();
  }

  void ctrl_ch(uchar c) {
    esc_if(alt);
    if (shift && !cfg.ctrl_exchange_shift) {
      // Send C1 control char if the charset supports it.
      // Otherwise prefix the C0 char with ESC.
      if (c < 0x20) {
        wchar wc = c | 0x80;
        int l = cs_wcntombn(buf + len, &wc, cs_cur_max, 1);
        if (l > 0 && buf[len] != '?') {
          len += l;
          return;
        }
      };
      esc_if(!alt);
    }
    ch(c);
  }

  bool ctrl_key(void) {
    bool try_appctrl(wchar wc) {
      switch (wc) {
        when '@' or '[' ... '_' or 'a' ... 'z':
          if (term.app_control & (1 << (wc & 0x1F))) {
            mods = ctrl * MDK_CTRL;
            other_code((wc & 0x1F) + '@');
            return true;
          }
      }
      return false;
    }

    bool try_key(void) {
      wchar wc = undead_keycode();  // should we fold that out into ctrl_key?

      if (try_appctrl(wc))
        return true;

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
      shift = is_key_down(VK_LSHIFT) & is_key_down(VK_RSHIFT);
      if (try_key())
        return true;
      shift = is_key_down(VK_SHIFT);
      if (shift || (key >= '0' && key <= '9' && !term.modify_other_keys)) {
        kbd[VK_SHIFT] ^= 0x80;
        if (try_key())
          return true;
        kbd[VK_SHIFT] ^= 0x80;
      }
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

  bool vk_special(string key_mapped) {
    if (!* key_mapped) {
      if (!layout())
        return false;
    }
    else if ((key_mapped[0] & ~037) == 0 && key_mapped[1] == 0)
      ctrl_ch(key_mapped[0]);
    else
      strcode(key_mapped);
    return true;
  }

  switch (key) {
    when VK_RETURN:
      if (!term.shortcut_override && cfg.window_shortcuts && alt && !ctrl) {
        trace_resize (("--- Alt-Enter (shift %d)", shift));
        send_syscommand(IDM_FULLSCREEN_ZOOM);
        return 1;
      }
      else
      if (extended && !numlock && term.app_keypad)
        //mod_ss3('M');
        app_pad_code('M' - '@');
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
      if (alt) {
        if (cfg.switch_shortcuts) {
          // does not work as Alt+TAB is not passed here anyway;
          // could try something with KeyboardHook:
          // http://www.codeproject.com/Articles/14485/Low-level-Windows-API-hooks-from-C-to-stop-unwante
          win_switch(shift, true);
          return 1;
        }
        else
          return 0;
      }
      if (!ctrl)
        shift ? csi('Z') : ch('\t');
      else if (cfg.switch_shortcuts) {
        win_switch(shift, false);
        return 1;
      }
      else
        term.modify_other_keys ? other_code('\t') : mod_csi('I');
    when VK_ESCAPE:
      term.app_escape_key
      ? ss3('[')
      : ctrl_ch(term.escape_sends_fs ? CTRL('\\') : CTRL('['));
    when VK_PAUSE:
      if (!vk_special(ctrl & !extended ? cfg.key_break : cfg.key_pause))
        return false;
    when VK_CANCEL:
      if (!vk_special(cfg.key_break))
        return false;
    when VK_SNAPSHOT:
      if (!vk_special(cfg.key_prtscreen))
        return false;
    when VK_APPS:
      if (!vk_special(cfg.key_menu))
        return false;
    when VK_SCROLL:
      if (!vk_special(cfg.key_scrlock))
        return false;
    when VK_F1 ... VK_F24:
      if (term.vt220_keys && ctrl && VK_F3 <= key && key <= VK_F10)
        key += 10, mods &= ~MDK_CTRL;
      if (key <= VK_F4)
        mod_ss3(key - VK_F1 + 'P');
      else {
        tilde_code(
          (uchar[]){
            15, 17, 18, 19, 20, 21, 23, 24, 25, 26,
            28, 29, 31, 32, 33, 34, 42, 43, 44, 45
          }[key - VK_F5]
        );
      }
    when VK_INSERT: edit_key(2, '0');
    when VK_DELETE: edit_key(3, '.');
    when VK_PRIOR:  edit_key(5, '9');
    when VK_NEXT:   edit_key(6, '3');
    when VK_HOME:   term.vt220_keys ? edit_key(1, '7') : cursor_key('H', '7');
    when VK_END:    term.vt220_keys ? edit_key(4, '1') : cursor_key('F', '1');
    when VK_UP:     cursor_key('A', '8');
    when VK_DOWN:   cursor_key('B', '2');
    when VK_LEFT:   cursor_key('D', '4');
    when VK_RIGHT:  cursor_key('C', '6');
    when VK_CLEAR:  cursor_key('E', '5');
    when VK_MULTIPLY ... VK_DIVIDE:
      if (key == VK_ADD && old_alt_state == ALT_ALONE)
        alt_state = ALT_HEX, alt_code = 0;
      else if (mods || (term.app_keypad && !numlock) || !layout())
        app_pad_code(key - VK_MULTIPLY + '*');
    when VK_NUMPAD0 ... VK_NUMPAD9:
      if ((term.app_cursor_keys || !term.app_keypad) &&
          alt_code_numpad_key(key - VK_NUMPAD0));
      else if (layout());
      else app_pad_code(key - VK_NUMPAD0 + '0');
    when 'A' ... 'Z' or ' ': {
      bool check_menu = key == VK_SPACE && !term.shortcut_override
                        && cfg.window_shortcuts && alt && !ctrl;
      if (altgr_key()) trace_key("altgr");
      else
      if (check_menu) {
        send_syscommand(SC_KEYMENU);
        return 1;
      }
      else
      if (key != ' ' && alt_code_key(key - 'A' + 0xA)) trace_key("alt");
      else if (char_key()) trace_key("char");
      else if (term.modify_other_keys > 1)
        modify_other_key();
      else if (ctrl_key()) trace_key("ctrl");
      else
        ctrl_ch(CTRL(key));
    }
    when '0' ... '9' or VK_OEM_1 ... VK_OEM_102:
      if (key <= '9' && alt_code_key(key - '0'));
      else if (char_key());
      else if (term.modify_other_keys <= 1 && ctrl_key());
      else if (term.modify_other_keys)
        modify_other_key();
      else if (zoom_hotkey());
      else if (key <= '9')
        app_pad_code(key);
      else if (VK_OEM_PLUS <= key && key <= VK_OEM_PERIOD)
        app_pad_code(key - VK_OEM_PLUS + '+');
    when VK_PACKET:
      if (!layout())
        return false;
    otherwise:
      if (!layout())
        return false;
  }

  hide_mouse();
  term_cancel_paste();

  if (len) {
    while (count--)
      child_send(buf, len);
  }

  return 1;
}

bool
win_key_up(WPARAM wp, LPARAM unused(lp))
{
#ifdef debug_virtual_key_codes
  printf("  win_key_up %s\n", vk_name((uint)wp));
#endif

  win_update_mouse();

  if (alt_F2_pending) {
    if ((uint)wp == VK_F2) {
      inline bool is_key_down(uchar vk) { return GetKeyState(vk) & 0x80; }
      if (is_key_down(VK_SHIFT))
        alt_F2_shifted = true;
      if (alt_F2_shifted)
        clone_size_token = false;

      alt_F2_pending = false;

      // Calculate heuristic approximation of selected monitor position
      int x, y;
      MONITORINFO mi;
      search_monitors(&x, &y, 0, alt_F2_home, &mi);
      RECT r = mi.rcMonitor;
      int refx, refy;
      if (alt_F2_monix < 0)
        refx = r.left + 10;
      else if (alt_F2_monix > 0)
        refx = r.right - 10;
      else
        refx = (r.left + r.right) / 2;
      if (alt_F2_moniy < 0)
        refy = r.top + 10;
      else if (alt_F2_monix > 0)
        refy = r.bottom - 10;
      else
        refy = (r.top + r.bottom) / 2;
      POINT pt;
      pt.x = refx + alt_F2_monix * x;
      pt.y = refy + alt_F2_moniy * y;
      // Find monitor over or nearest to point
      HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
      int moni = search_monitors(&x, &y, mon, true, 0);

#ifdef debug_multi_monitors
      printf("NEW @ %d,%d @ monitor %d\n", pt.x, pt.y, moni);
#endif
      send_syscommand2(IDM_NEW_MONI, ' ' + moni);
    }
  }

  if (wp != VK_MENU)
    return false;

  if (alt_state > ALT_ALONE && alt_code) {
    if (cs_cur_max < 4) {
      char buf[4];
      int pos = sizeof buf;
      do
        buf[--pos] = alt_code;
      while (alt_code >>= 8);
      child_send(buf + pos, sizeof buf - pos);
    }
    else if (alt_code < 0x10000) {
      wchar wc = alt_code;
      if (wc < 0x20)
        MultiByteToWideChar(CP_OEMCP, MB_USEGLYPHCHARS,
                            (char[]){wc}, 1, &wc, 1);
      child_sendw(&wc, 1);
    }
    else {
      xchar xc = alt_code;
      child_sendw((wchar[]){high_surrogate(xc), low_surrogate(xc)}, 2);
    }
  }

  alt_state = ALT_NONE;
  return true;
}
