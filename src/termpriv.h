#ifndef TERMPRIV_H
#define TERMPRIV_H

/*
 * Internal terminal functions, types and structs.
 */

#include "term.h"

#define incpos(p) ((p).x == term.cols ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0))
#define decpos(p) ((p).x == 0 ? ((p).x = term.cols, (p).y--, 1) : ((p).x--, 0))

#define poslt(p1,p2) ((p1).y < (p2).y || ((p1).y == (p2).y && (p1).x < (p2).x))
#define posle(p1,p2) ((p1).y < (p2).y || ((p1).y == (p2).y && (p1).x <= (p2).x))
#define poseq(p1,p2) ((p1).y == (p2).y && (p1).x == (p2).x)
#define posdiff(p1,p2) (((p1).y - (p2).y) * (term.cols + 1) + (p1).x - (p2).x)

/* Product-order comparisons for rectangular block selection. */
#define posPlt(p1,p2) ((p1).y <= (p2).y && (p1).x < (p2).x)
#define posPle(p1,p2) ((p1).y <= (p2).y && (p1).x <= (p2).x)


extern void term_print_finish(void);

extern void term_schedule_cblink(void);
extern void term_schedule_vbell(int already_started, int startpoint);

extern void term_switch_screen(bool to_alt, bool reset);
extern void term_set_status_type(int type, int lines);
extern void term_switch_status(bool status_line);
extern void term_clear_status(void);
extern void term_check_boundary(int x, int y);
extern void term_do_scroll(int topline, int botline, int lines, bool sb);
extern void term_erase(bool selective, bool line_only, bool from_begin, bool to_end);
extern int  term_last_nonempty_line(void);

/* Bidi paragraph support */
extern void clear_wrapcontd(termline * line, int y);
extern ushort getparabidi(termline * line);
extern wchar * wcsline(termline * line);  // for debug output

static inline bool
term_selecting(void)
{ return term.mouse_state < 0 && term.mouse_state >= MS_SEL_LINE; }

extern void term_update_cs(void);
extern uchar scriptfont(ucschar ch);

extern void clear_emoji_data(void);
extern char * get_emoji_description(termchar *);

extern int termchars_equal(termchar * a, termchar * b);
extern int termchars_equal_override(termchar * a, termchar * b, uint bchr, cattr battr);
extern int termattrs_equal_fg(cattr * a, cattr * b);

extern void copy_termchar(termline * destline, int x, termchar * src);
extern void move_termchar(termline * line, termchar * dest, termchar * src);

extern void add_cc(termline *, int col, wchar chr, cattr attr);
extern void clear_cc(termline *, int col);

extern uchar * compressline(termline *);
extern termline * decompressline(uchar *, int * bytes_used);

extern termchar * term_bidi_line(termline *, int scr_y);

extern void term_export_html(bool do_open);
extern char * term_get_html(int level);
extern void print_screen(void);

extern int putlink(char * link);
extern char * geturl(int n);

extern void compose_clear(void);

/* Direct screen buffer output, for status line */
extern void write_char(wchar wc, int width);
extern void write_ucschar(wchar hwc, wchar wc, int width);

#endif
