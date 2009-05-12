#ifndef TERM_H
#define TERM_H

#include "termline.h"
#include "tree234.h"
#include "bufchain.h"
#include "print.h"
#include "minibidi.h"

/* Three attribute types: 
 * The ATTRs (normal attributes) are stored with the characters in
 * the main display arrays
 *
 * The TATTRs (temporary attributes) are generated on the fly, they
 * can overlap with characters but not with normal attributes.
 *
 * The LATTRs (line attributes) are an entirely disjoint space of
 * flags.
 * 
 * The DATTRs (display attributes) are internal to terminal.c (but
 * defined here because their values have to match the others
 * here); they reuse the TATTR_* space but are always masked off
 * before sending to the front end.
 *
 * ATTR_INVALID is an illegal colour combination.
 */

#define TATTR_ACTCURS 	    0x40000000UL        /* active cursor (block) */
#define TATTR_PASCURS 	    0x20000000UL        /* passive cursor (box) */
#define TATTR_RIGHTCURS	    0x10000000UL        /* cursor-on-RHS */
#define TATTR_COMBINING	    0x80000000UL        /* combining characters */

#define DATTR_STARTRUN      0x80000000UL        /* start of redraw run */

#define TDATTR_MASK         0xF0000000UL
#define TATTR_MASK (TDATTR_MASK)
#define DATTR_MASK (TDATTR_MASK)

#define LATTR_NORM   0x00000000UL
#define LATTR_WIDE   0x00000001UL
#define LATTR_TOP    0x00000002UL
#define LATTR_BOT    0x00000003UL
#define LATTR_MODE   0x00000003UL
#define LATTR_WRAPPED 0x00000010UL      /* this line wraps to next */
#define LATTR_WRAPPED2 0x00000020UL     /* with WRAPPED: CJK wide character
                                         * wrapped to next line, so last
                                         * single-width cell is empty */
#define ATTR_INVALID 0x03FFFFU
#define ATTR_NARROW  0x800000U
#define ATTR_WIDE    0x400000U
#define ATTR_BOLD    0x040000U
#define ATTR_UNDER   0x080000U
#define ATTR_REVERSE 0x100000U
#define ATTR_BLINK   0x200000U
#define ATTR_FGMASK  0x0001FFU
#define ATTR_BGMASK  0x03FE00U
#define ATTR_COLOURS 0x03FFFFU
#define ATTR_FGSHIFT 0
#define ATTR_BGSHIFT 9

/*
 * The definitive list of colour numbers stored in terminal
 * attribute words is kept here. It is:
 * 
 *  - 0-7 are ANSI colours (KRGYBMCW).
 *  - 8-15 are the bold versions of those colours.
 *  - 16-255 are the remains of the xterm 256-colour mode (a
 *    216-colour cube with R at most significant and B at least,
 *    followed by a uniform series of grey shades running between
 *    black and white but not including either on grounds of
 *    redundancy).
 *  - 256 is default foreground
 *  - 257 is default bold foreground
 *  - 258 is default background
 *  - 259 is default bold background
 *  - 260 is cursor foreground
 *  - 261 is cursor background
 */

#define ATTR_DEFFG   (256 << ATTR_FGSHIFT)
#define ATTR_DEFBG   (258 << ATTR_BGSHIFT)
#define ATTR_DEFAULT (ATTR_DEFFG | ATTR_DEFBG)

#define TTYPE termchar
#define TSIZE (sizeof(TTYPE))

typedef struct {
  int y, x;
} pos;

typedef enum {
  MB_NONE = 0, MBT_LEFT = 1, MBT_MIDDLE = 2, MBT_RIGHT = 3
} mouse_button;

typedef enum {
  SHIFT = 1, ALT = 2, CTRL = 4
} mod_keys;

enum {
  ARGS_MAX = 32,     /* max # of esc sequence arguments */
  ARG_DEFAULT = 0   /* if an arg isn't specified */
};

enum { OSC_STR_MAX = 2048 };

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
  bool app_keypad;
  bool newline_mode;
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
  bool osc_w;

  uchar *tabs;

 /*
  * DO_CTRLS here isn't an actual state, but acts as a marker that
  * divides the states in two classes.
  */
  enum {
    TOPLEVEL, SEEN_ESC, SEEN_CSI, SEEN_OSC, SEEN_OSC_W,
    DO_CTRLS, SEEN_OSC_P, OSC_STRING, OSC_MAYBE_ST,
    SEEN_DCS, DCS_MAYBE_ST
  } state;

  enum {
    MM_NONE, MM_X10, MM_VT200, MM_BTN_EVENT, MM_ANY_EVENT
  } mouse_mode;

  enum {
    MS_CLICKED = -1, MS_IDLE = 0,
    MS_SEL_CHAR = 1, MS_SEL_WORD = 2, MS_SEL_LINE = 3
  } mouse_state;
  
  bool sel_rect, selected;
  pos sel_start, sel_end, sel_anchor;
  
 /* Scroll steps during selection when cursor out of window. */
  int sel_scroll;
  pos sel_pos;

 /* Mask of attributes to pay attention to when painting. */
  int attr_mask;

  wchar *paste_buffer;
  int paste_len, paste_pos;

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

void term_init(void);
void term_resize(int, int);
void term_scroll(int, int);
void term_reset(void);
void term_clear_scrollback(void);
void term_mouse_click(mouse_button, mod_keys, pos, int count);
void term_mouse_release(mouse_button, mod_keys, pos);
void term_mouse_move(mouse_button, mod_keys, pos);
void term_mouse_wheel(int delta, int lines_per_notch, mod_keys, pos);
void term_deselect(void);
void term_select_all(void);
void term_paint(void);
void term_update(void);
void term_invalidate(int left, int top, int right, int bottom);
void term_invalidate_all(void);
void term_blink(int set_cursor);
void term_copy(void);
void term_paste(wchar *, uint len);
void term_send_paste(void);
void term_cancel_paste(void);
void term_reconfig(void);
void term_seen_key_event(void);
void term_write(const char *, int len);
void term_set_focus(int has_focus);
bool term_in_utf(void);

#endif
