#ifndef TERMPRIV_H
#define TERMPRIV_H

#include "term.h"
#include "termline.h"

#include "tree234.h"
#include "print.h"
#include "unicode.h"
#include "minibidi.h"
#include "bufchain.h"
#include "config.h"


#define incpos(p) ((p).x == term.cols ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0))
#define decpos(p) ((p).x == 0 ? ((p).x = term.cols, (p).y--, 1) : ((p).x--, 0))

#define poslt(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x < (p2).x ) )
#define posle(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x <= (p2).x ) )
#define poseq(p1,p2) ( (p1).y == (p2).y && (p1).x == (p2).x )
#define posdiff(p1,p2) ( ((p1).y - (p2).y) * (term.cols+1) + (p1).x - (p2).x )

/* Product-order comparisons for rectangular block selection. */
#define posPlt(p1,p2) ( (p1).y <= (p2).y && (p1).x < (p2).x )
#define posPle(p1,p2) ( (p1).y <= (p2).y && (p1).x <= (p2).x )


/*
 * Internal terminal functions and structs.
 */

void term_print_setup(void);
void term_print_finish(void);
void term_print_flush(void);

void term_schedule_tblink(void);
void term_schedule_cblink(void);
void term_schedule_update(void);
void term_schedule_vbell(int already_started, int startpoint);

void term_swap_screen(int which, int reset, int keep_cur_pos);
void term_check_selection(pos from, pos to);
void term_check_boundary(int x, int y);
void term_do_scroll(int topline, int botline, int lines, int sb);
void term_erase_lots(int line_only, int from_begin, int to_end);

#define ARGS_MAX 32     /* max # of esc sequence arguments */
#define ARG_DEFAULT 0   /* if an arg isn't specified */
#define def(a,d) ( (a) == ARG_DEFAULT ? (d) : (a) )

#define OSC_STR_MAX 2048

typedef struct belltime {
  struct belltime *next;
  uint ticks;
} belltime;

struct term {
  int compatibility_level;

  tree234 *scrollback;  /* lines scrolled off top of screen */
  tree234 *screen;      /* lines on primary screen */
  tree234 *alt_screen;  /* lines on alternate screen */
  int disptop;  /* distance scrolled back (0 or -ve) */
  int tempsblines;      /* number of lines of .scrollback that
                         * can be retrieved onto the terminal
                         * ("temporary scrollback") */

  termline **disptext;  /* buffer of text on real screen */
  int dispcursx, dispcursy;     /* location of cursor on real screen */
  int curstype; /* type of cursor on real screen */

  belltime *bellhead, *belltail;
  int nbells;
  int bell_overloaded;
  int lastbell;

  int default_attr, curr_attr, save_attr;
  termchar basic_erase_char, erase_char;

  bufchain *inbuf;      /* terminal input buffer */
  pos curs;     /* cursor */
  pos savecurs; /* saved cursor position */
  int marg_t, marg_b;   /* scroll margins */
  int dec_om;   /* DEC origin mode flag */
  int wrap, wrapnext;   /* wrap flags */
  int insert;   /* insert-mode flag */
  int cset;     /* 0 or 1: which char set */
  int save_cset, save_csattr;   /* saved with cursor position */
  int save_utf, save_wnext;     /* saved with cursor position */
  int rvideo;   /* global reverse video flag */
  uint rvbell_startpoint;       /* for ESC[?5hESC[?5l vbell */
  int cursor_on;        /* cursor enabled flag */
  int reset_132;        /* Flag ESC c resets to 80 cols */
  int use_bce;  /* Use Background coloured erase */
  int cblinker; /* When blinking is the cursor on ? */
  int tblinker; /* When the blinking text is on */
  int blink_is_real;    /* Actually blink blinking text */
  int echoing;  /* Does terminal want local echo? */
  int editing;  /* Does terminal want local edit? */
  int sco_acs, save_sco_acs;    /* CSI 10,11,12m -> OEM charset */
  int vt52_bold;        /* Force bold on non-bold colours */
  int utf;      /* Are we in toggleable UTF-8 mode? */
  int utf_state;        /* Is there a pending UTF-8 character */
  int utf_char; /* and what is it so far. */
  int utf_size; /* The size of the UTF character. */
  int printing, only_printing;  /* Are we doing ANSI printing? */
  int print_state;      /* state of print-end-sequence scan */
  bufchain *printer_buf;        /* buffered data for printer */
  printer_job *print_job;

 /* ESC 7 saved state for the alternate screen */
  pos alt_savecurs;
  int alt_save_attr;
  int alt_save_cset, alt_save_csattr;
  int alt_save_utf, alt_save_wnext;
  int alt_save_sco_acs;

  int rows, cols;
  bool has_focus;
  bool in_vbell;
  bool app_cursor_keys;
  bool cr_lf_return;
  bool seen_disp_event;
  bool big_cursor;

  int cset_attr[2];

 /*
  * Saved settings on the alternate screen.
  */
  int alt_x, alt_y, alt_om, alt_wrap, alt_wnext, alt_ins;
  int alt_cset, alt_sco_acs, alt_utf;
  int alt_t, alt_b;
  int which_screen;

  int esc_args[ARGS_MAX];
  int esc_nargs;
  int esc_query;

  int osc_strlen;
  char osc_string[OSC_STR_MAX + 1];
  int osc_w;

  char id_string[1024];

  ubyte *tabs;

  /*
   * DO_CTRLS here isn't an actual state, but acts as a marker that
   * divides the states in two classes.
   */
  enum {
    TOPLEVEL, SEEN_ESC, SEEN_CSI, SEEN_OSC, SEEN_OSC_W,
    DO_CTRLS, SEEN_OSC_P, OSC_STRING, OSC_MAYBE_ST
  } state;

  enum {
    MT_NONE, MT_X10, MT_VT200, MT_BTN_EVENT, MT_ANY_EVENT
  } mouse_tracking;

  enum {
    MS_CLICKED = -1, MS_IDLE = 0,
    MS_SEL_CHAR = 1, MS_SEL_WORD = 2, MS_SEL_LINE = 3
  } mouse_state;
  
  bool sel_rect, selected;
  pos sel_start, sel_end, sel_anchor;
  

 /* Mask of attributes to pay attention to when painting. */
  int attr_mask;

  wchar *paste_buffer;
  int paste_len, paste_pos, paste_hold;
  int last_paste;

 /*
  * We maintain a full _copy_ of a Config structure here, not
  * merely a pointer to it. That way, when we're passed a new
  * one for reconfiguration, we can check the differences and
  * adjust the _current_ setting of (e.g.) auto wrap mode rather
  * than only the default.
  */
  config cfg;

 /*
  * child_read calls term_write, but it can also be called from
  * the ldisc if the ldisc is called _within_ term_out. So we
  * have to guard against re-entrancy - if from_backend is
  * called recursively like this, it will simply add data to the
  * end of the buffer term_out is in the process of working
  * through.
  */
  bool in_term_write;

 /*
  * We schedule a window update shortly after receiving terminal
  * data. This tracks whether one is currently pending.
  */
  bool update_pending;

 /*
  * Track pending blinks and tblinks.
  */
  bool tblink_pending, cblink_pending;

 /*
  * These are buffers used by the bidi and Arabic shaping code.
  */
  termchar *ltemp;
  int ltemp_size;
  bidi_char *wcFrom, *wcTo;
  int wcFromTo_size;
  bidi_cache_entry *pre_bidi_cache, *post_bidi_cache;
  int bidi_cache_size;
};

extern struct term term;

#endif
