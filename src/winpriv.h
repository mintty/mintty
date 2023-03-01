#ifndef WINPRIV_H
#define WINPRIV_H

#include "win.h"
#include "winids.h"

#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <imm.h>
#define SB_PRIOR 100
#define SB_NEXT 101

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

extern HINSTANCE inst;  // The all-important instance handle
extern HWND wnd;        // the main terminal window
extern HIMC imc;        // the input method context
extern HWND config_wnd; // the options window
extern ATOM class_atom;

extern void clear_tabs(void);
extern void add_tab(uint tabi, HWND wndi);
// Inter-window actions
enum {
  WIN_MINIMIZE = 0,
  WIN_MAXIMIZE = -1,
  WIN_FULLSCREEN = -2,
  WIN_TOP = 1,
  WIN_TITLE = 4,
  WIN_INIT_POS = 5,
  WIN_HIDE = 8,
};
// support tabbar
extern void win_to_top(HWND top_wnd);
extern void win_post_sync_msg(HWND target, int level);
struct tabinfo {
  unsigned long tag;
  HWND wnd;
  wchar * title;
};
extern struct tabinfo * tabinfo;
extern int ntabinfo;

extern COLORREF colours[COLOUR_NUM];
extern colour brighten(colour c, colour against, bool monotone);

extern LOGFONT lfont;

extern int font_size;  // logical font size, as configured (< 0: pixel size)
extern int cell_width, cell_height;  // includes spacing
extern bool font_ambig_wide;
extern int line_scale;
extern int PADDING;
extern int OFFSET;
extern bool show_charinfo;
extern void toggle_charinfo(void);
extern void toggle_vt220(void);
extern char * fontpropinfo(void);

extern bool title_settable;
extern bool support_wsl;
extern wchar * wslname;
extern wstring wsl_basepath;
extern bool report_config;
extern bool report_child_pid;
extern bool report_child_tty;

extern int ini_width, ini_height;
extern bool win_is_fullscreen;
extern bool win_is_always_on_top;
extern bool clipboard_token;
extern uint dpi;
extern int per_monitor_dpi_aware;
extern bool keep_screen_on;
extern bool force_opaque;

extern bool click_focus_token;
extern pos last_pos;
extern int lines_scrolled;
extern bool kb_input;
extern uint kb_trace;

extern void win_update_now(void);

extern bool fill_background(HDC dc, RECT * boxp);
extern void win_flush_background(bool clearbg);
extern void win_paint(void);

extern void win_init_fonts(int size, bool allfonts);
extern wstring win_get_font(uint findex);
extern void win_change_font(uint findex, wstring fn);
extern void win_font_cs_reconfig(bool font_changed);

extern void win_keep_screen_on(bool);

extern void win_update_scrollbar(bool inner);
extern void win_set_scrollview(int pos, int len, int height);

extern void win_adapt_term_size(bool sync_size_with_font, bool scale_font_with_size);
extern void scale_to_image_ratio(void);

extern void win_open_config(void);
extern void * load_library_func(string lib, string func);
extern void update_available_version(bool ok);
extern void set_dpi_auto_scaling(bool on);
extern void win_update_transparency(int transparency, bool opaque);
extern void win_prefix_title(const wstring);
extern void win_unprefix_title(const wstring);
extern void win_set_icon(char * s, int icon_index);

extern void win_show_tip(int x, int y, int cols, int rows);
extern void win_destroy_tip(void);

extern void taskbar_progress(int percent);
extern HCURSOR win_get_cursor(bool appmouse);
extern void set_cursor_style(bool appmouse, wchar * style);

extern void win_init_menus(void);
extern void win_update_menus(bool callback);
extern void user_function(wstring commands, int n);

extern void win_show_mouse(void);
extern bool win_mouse_click(mouse_button, LPARAM);
extern void win_mouse_release(mouse_button, LPARAM);
extern void win_mouse_wheel(POINT wpos, bool horizontal, int delta);
extern void win_mouse_move(bool nc, LPARAM);

extern mod_keys get_mods(void);
extern void win_key_reset(void);
extern void provide_input(wchar);
extern bool win_key_down(WPARAM, LPARAM);
extern bool win_key_up(WPARAM, LPARAM);
extern void do_win_key_toggle(int vk, bool on);
extern void win_csi_seq(char * pre, char * suf);

extern void win_led(int led, bool set);
extern bool get_scroll_lock(void);
extern void sync_scroll_lock(bool locked);

extern wchar * dewsl(wchar * wpath);
extern void shell_exec(wstring wpath);
extern void win_init_drop_target(void);

extern wstring wslicon(wchar * params);

extern char * foreground_cwd(void);

extern void toggle_status_line(void);

extern void win_switch(bool back, bool alternate);
extern int sync_level(void);

extern int search_monitors(int * minx, int * miny, HMONITOR lookup_mon, int get_primary, MONITORINFO *mip);

extern void win_set_ime_open(bool);
extern void win_set_ime(bool open);
extern bool win_get_ime(void);

extern void win_dark_mode(HWND w);

extern void show_message(char * msg, UINT type);
extern void show_info(char * msg);

extern void win_close(void);
extern void win_toggle_on_top(void);

extern unsigned long mtime(void);

extern void term_save_image(bool do_open);

#endif
