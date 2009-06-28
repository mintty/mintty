#ifndef TERMPRIV_H
#define TERMPRIV_H

/*
 * Internal terminal functions and structs.
 */

#include "term.h"

#include "unicode.h"
#include "config.h"

enum {
  CL_ANSIMIN   = 0x0001, /* Codes in all ANSI like terminals. */
  CL_VT100     = 0x0002, /* VT100 */
  CL_VT100AVO  = 0x0004, /* VT100 +AVO; 132x24 (not 132x14) & attrs */
  CL_VT102     = 0x0008, /* VT102 */
  CL_VT220     = 0x0010, /* VT220 */
  CL_VT320     = 0x0020, /* VT320 */
  CL_VT420     = 0x0040, /* VT420 */
  CL_VT510     = 0x0080, /* VT510, NB VT510 includes ANSI */
  CL_VT340TEXT = 0x0100, /* VT340 extensions that appear in the VT420 */
  CL_SCOANSI   = 0x1000, /* SCOANSI not in ANSIMIN. */
  CL_ANSI      = 0x2000, /* ANSI ECMA-48 not in the VT100..VT420 */
  CL_OTHER     = 0x4000, /* Others, Xterm, linux, putty, dunno, etc */
};

enum {
  TM_VT100    = CL_ANSIMIN | CL_VT100,
  TM_VT100AVO = TM_VT100 | CL_VT100AVO,
  TM_VT102    = TM_VT100AVO | CL_VT102,
  TM_VT220    = TM_VT102 | CL_VT220,
  TM_VTXXX    = TM_VT220 | CL_VT340TEXT | CL_VT510 | CL_VT420 | CL_VT320,
  TM_SCOANSI  = CL_ANSIMIN | CL_SCOANSI,
  TM_MINTTY   = TM_VTXXX | CL_ANSI | CL_OTHER,
  TM_PUTTY    = 0xFFFF
};

#define incpos(p) ((p).x == term.cols ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0))
#define decpos(p) ((p).x == 0 ? ((p).x = term.cols, (p).y--, 1) : ((p).x--, 0))

#define poslt(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x < (p2).x ) )
#define posle(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x <= (p2).x ) )
#define poseq(p1,p2) ( (p1).y == (p2).y && (p1).x == (p2).x )
#define posdiff(p1,p2) ( ((p1).y - (p2).y) * (term.cols+1) + (p1).x - (p2).x )

/* Product-order comparisons for rectangular block selection. */
#define posPlt(p1,p2) ( (p1).y <= (p2).y && (p1).x < (p2).x )
#define posPle(p1,p2) ( (p1).y <= (p2).y && (p1).x <= (p2).x )

void term_print_setup(void);
void term_print_finish(void);
void term_print_flush(void);

void term_schedule_tblink(void);
void term_schedule_cblink(void);
void term_schedule_vbell(int already_started, int startpoint);

void term_swap_screen(int which, int reset, int keep_cur_pos);
void term_check_boundary(int x, int y);
void term_do_scroll(int topline, int botline, int lines, int sb);
void term_erase_lots(int line_only, int from_begin, int to_end);

static inline bool
term_selecting(void)
{ return term.mouse_state >= MS_SEL_CHAR; }

#endif
