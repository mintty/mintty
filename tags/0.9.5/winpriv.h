#ifndef WINPRIV_H
#define WINPRIV_H

#include "win.h"
#include "winids.h"

#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <imm.h>

extern HINSTANCE inst;  // The all-important instance handle
extern HWND wnd;        // the main terminal window
extern HIMC imc;        // the input method context
extern HWND config_wnd; // the options window

extern COLORREF colours[COLOUR_NUM];

extern LOGFONT lfont;

extern enum bold_mode { BOLD_COLOURS, BOLD_SHADOW, BOLD_FONT } bold_mode;

extern int font_size;
extern int font_width, font_height;
extern bool font_ambig_wide;

enum { PADDING = 1 };

void win_paint(void);

void win_init_fonts(void);
void win_deinit_fonts(void);

void win_open_config(void);

void win_show_tip(int x, int y, int cols, int rows);
void win_destroy_tip(void);

void win_init_menus(void);
void win_update_menus(void);

void win_show_mouse(void);
void win_mouse_click(mouse_button, LPARAM);
void win_mouse_release(LPARAM);
void win_mouse_wheel(WPARAM, LPARAM);
void win_mouse_move(bool nc, LPARAM);

bool win_key_down(WPARAM, LPARAM);
bool win_key_up(WPARAM, LPARAM);

void win_init_drop_target(void);

void win_copy_title(void);

void win_switch(bool back);

void win_set_ime_open(bool);

bool win_is_fullscreen;

DWORD (WINAPI *pGetGlyphIndicesW)(HDC,LPCWSTR,int,LPWORD,DWORD);

#endif
