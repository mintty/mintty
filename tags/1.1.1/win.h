#ifndef WIN_H
#define WIN_H

#include "term.h"

void win_reconfig(void);

void win_update(void);
void win_schedule_update(void);

void win_text(int x, int y, wchar *text, int len, uint attr, int lattr);
void win_update_mouse(void);
void win_capture_mouse(void);
void win_bell(void);

void win_set_title(char *);
void win_save_title(void);
void win_restore_title(void);

colour win_get_colour(colour_i);
void win_set_colour(colour_i, colour);
void win_reset_colours(void);
colour win_get_sys_colour(bool fg);

void win_invalidate_all(void);

void win_set_pos(int x, int y);
void win_set_chars(int rows, int cols);
void win_set_pixels(int height, int width);
void win_maximise(int max);
void win_set_zorder(bool top);
void win_set_iconic(bool);
void win_update_scrollbar(void);
bool win_is_iconic(void);
void win_get_pos(int *xp, int *yp);
void win_get_pixels(int *height_p, int *width_p);
void win_get_screen_chars(int *rows_p, int *cols_p);
void win_popup_menu(void);

void win_zoom_font(int);
void win_set_font_size(int);
uint win_get_font_size(void);

void win_check_glyphs(wchar *wcs, uint num);

void win_open(wstring path);
void win_copy(const wchar *data, uint *attrs, int len);
void win_paste(void);

void win_set_timer(void_fn cb, uint ticks);

void win_show_about(void);
void win_show_error(wchar *);

bool win_is_glass_available(void);

int get_tick_count(void);
int cursor_blink_ticks(void);

int win_char_width(xchar);
wchar win_combine_chars(wchar bc, wchar cc);
extern wchar win_linedraw_chars[31];

#endif
