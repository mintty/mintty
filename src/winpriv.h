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

extern HINSTANCE inst;  // The all-important instance handle
extern HWND wnd;        // the main terminal window
extern HIMC imc;        // the input method context
extern HWND config_wnd; // the options window

extern COLORREF colours[COLOUR_NUM];
extern colour brighten(colour c, colour against);

extern LOGFONT lfont;

extern int font_size;  // logical font size, as configured (< 0: pixel size)
extern int cell_width, cell_height;  // includes spacing
extern int PADDING;
extern bool show_charinfo;
extern void toggle_charinfo(void);
extern char * fontpropinfo(void);

extern bool support_wsl;
extern wchar * wsl_basepath;

extern bool win_is_fullscreen;
extern uint dpi;
extern bool per_monitor_dpi_aware;

extern void win_paint(void);

extern void win_init_fonts(int size);
extern wstring win_get_font(uint findex);
extern void win_change_font(uint findex, wstring fn);
extern void win_font_cs_reconfig(bool font_changed);

extern void win_adapt_term_size(bool sync_size_with_font, bool scale_font_with_size);

extern void win_open_config(void);
extern void * load_library_func(string lib, string func);
extern void update_available_version(bool ok);
extern void set_dpi_auto_scaling(bool on);
extern void win_update_transparency(bool opaque);
extern void win_prefix_title(const wstring);
extern void win_unprefix_title(const wstring);

extern void win_show_tip(int x, int y, int cols, int rows);
extern void win_destroy_tip(void);

extern void win_init_menus(void);
extern void win_update_menus(void);

extern void win_show_mouse(void);
extern void win_mouse_click(mouse_button, LPARAM);
extern void win_mouse_release(mouse_button, LPARAM);
extern void win_mouse_wheel(WPARAM, LPARAM);
extern void win_mouse_move(bool nc, LPARAM);

extern void win_key_reset(void);
extern bool win_key_down(WPARAM, LPARAM);
extern bool win_key_up(WPARAM, LPARAM);

extern void win_init_drop_target(void);

extern void win_switch(bool back, bool alternate);
extern int search_monitors(int * minx, int * miny, HMONITOR lookup_mon, bool get_primary, MONITORINFO *mip);

extern void win_set_ime_open(bool);

extern void show_message(char * msg, UINT type);

#endif
