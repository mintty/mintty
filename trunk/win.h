#ifndef WIN_H
#define WIN_H

#include "platform.h"

void win_reconfig();

void win_update(void);
void win_schedule_update(void);

int  win_char_width(int uc);
void win_text(int, int, wchar *, int, uint, int);
void win_cursor(int, int, wchar *, int, uint, int);
void win_set_sys_cursor(int x, int y);
void win_update_mouse(void);
void win_capture_mouse(void);
void win_bell(int);

void win_set_title(char *);
void win_set_sbar(int, int, int);

enum { FG_COLOUR_I = 256, BG_COLOUR_I = 258, CURSOR_COLOUR_I = 261 };
colour win_get_colour(uint i);
void win_set_colour(uint i, colour);
void win_reset_colours(void);
void win_invalidate_all(void);

void win_move(int x, int y);
void win_resize(int rows, int cols);
void win_zoom_font(int zoom);
void win_set_zoom(bool);
void win_set_zorder(bool top);
void win_set_iconic(bool);
bool win_is_iconic(void);
void win_get_pos(int *x, int *y);
void win_get_pixels(int *x, int *y);
void win_popup_menu(void);

void win_copy(wchar *, int *attrs, int len);
void win_paste(void);

void win_set_timer(void_fn cb, uint ticks);

void win_show_about(void);

#endif
