#ifndef TERMPRIV_H
#define TERMPRIV_H

/*
 * Internal terminal functions and structs.
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

void term_print_finish(void);

void term_schedule_tblink(void);
void term_schedule_cblink(void);
void term_schedule_vbell(int already_started, int startpoint);

void term_switch_screen(bool to_alt, bool reset);
void term_check_boundary(int x, int y);
void term_do_scroll(int topline, int botline, int lines, bool sb);
void term_erase(bool selective, bool line_only, bool from_begin, bool to_end);
int  term_last_nonempty_line(void);

static inline bool
term_selecting(void)
{ return term.mouse_state < 0 && term.mouse_state >= MS_SEL_LINE; }

void term_update_cs(void);

#endif
