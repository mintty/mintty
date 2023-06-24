#ifndef WIN_H
#define WIN_H

#include "term.h"

#define dont_debug_resize

#ifdef debug_resize
#define trace_resize(params)	printf params
#else
#define trace_resize(params)
#endif

extern char * home;
extern char * cmd;
extern bool icon_is_from_shortcut;
extern wstring shortcut;

extern bool cygver_ge(uint major, uint minor);

extern void exit_mintty(void);
extern void report_pos(void);
extern void win_reconfig(void);

extern void win_update(bool update_sel_tip);
extern void win_schedule_update(void);
extern void do_update(void);

extern void win_text(int x, int y, wchar *text, int len, cattr attr, cattr *textattr, ushort lattr, char has_rtl, bool clearpad, uchar phase);

/* input */
extern void win_update_mouse(void);
extern void win_capture_mouse(void);
extern void win_get_locator_info(int *x, int *y, int *buttons, bool by_pixels);
extern wchar * char_code_indication(uint * what);

/* beep */
extern void win_beep(uint tone, float vol, float freq, uint ms);
extern void win_sound(char * sound_name, uint options);
extern void win_bell(config *);
extern void win_margin_bell(config *);

/* title */
extern void win_set_title(char *);
extern void win_save_title(void);
extern void win_restore_title(void);
extern void win_copy_title(void);
extern char * win_get_title(void);
extern void win_copy_text(const char *s);

/* colour */
extern colour win_get_colour(colour_i);
extern void win_set_colour(colour_i, colour);
extern void win_reset_colours(void);
extern colour win_get_sys_colour(int colid);
extern uint colour_dist(colour a, colour b);
extern colour truecolour(cattr *, colour bg);

extern void win_invalidate_all(bool clearbg);

extern int horclip(void);
extern void horscroll(int cells);
extern void horscrollto(int percent);
extern void horsizing(int cells, bool from_right);

extern void win_set_pos(int x, int y);
extern void win_set_chars(int rows, int cols);
extern void win_set_pixels(int height, int width);
extern void win_set_geom(int y, int x, int height, int width);
extern void win_maximise(int max);
extern void win_set_zorder(bool top);
extern void win_set_iconic(bool);
extern bool win_is_iconic(void);
extern void win_get_scrpos(int *xp, int *yp, bool with_borders);
extern void win_get_pixels(int *height_p, int *width_p, bool with_borders);
extern void win_get_screen_chars(int *rows_p, int *cols_p);
extern void win_popup_menu(mod_keys mods);
extern bool win_title_menu(bool leftbut);

extern void win_zoom_font(int, bool sync_size_with_font);
extern void win_set_font_size(int, bool sync_size_with_font);
extern uint win_get_font_size(void);

extern void win_check_glyphs(wchar *wcs, uint num, cattrflags attr);
extern wchar get_errch(wchar *wcs, cattrflags attr);
extern int win_char_width(xchar, cattrflags attr);
extern wchar win_combine_chars(wchar bc, wchar cc, cattrflags attr);

extern void win_open(wstring path, bool adjust_dir);
extern void win_copy(const wchar *data, cattr *cattrs, int len);
extern void win_copy_as(const wchar *data, cattr *cattrs, int len, char what);
extern void win_paste(void);
extern void win_paste_path(void);

extern void win_set_timer(void_fn cb, uint ticks);

extern bool print_opterror(FILE * stream, string msg, bool utf8params, string p1, string p2);
extern void win_show_about(void);
extern void win_show_error(char * msg);
extern void win_show_warning(char * msg);
extern int message_box(HWND parwnd, char * wtext, char * wcaption, int type, wstring ok);
extern int message_box_w(HWND parwnd, wchar * wtext, wchar * wcaption, int type, wstring ok);

extern bool win_is_glass_available(void);

extern int get_tick_count(void);
extern int cursor_blink_ticks(void);

extern wchar win_linedraw_char(int i);

typedef enum {
  ACM_TERM = 1,        /* actual terminal rendering */
  ACM_RTF_PALETTE = 2, /* winclip - rtf palette setup stage */
  ACM_RTF_GEN = 4,     /* winclip - rtf generation stage */
  ACM_SIMPLE = 8,      /* simplified (bold, [rvideo,] dim, invisible) */
  ACM_VBELL_BG = 16,   /* visual-bell background highlight */
} attr_colour_mode;

extern cattr apply_attr_colour(cattr a, attr_colour_mode mode);

#endif
