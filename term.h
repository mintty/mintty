#ifndef TERM_H
#define TERM_H

#include "minibidi.h"
#include "config.h"

// Colour numbers

typedef enum {
  // ANSI colours
  BLACK_I   = 0,
  RED_I     = 1,
  GREEN_I   = 2,
  YELLOW_I  = 3,
  BLUE_I    = 4,
  MAGENTA_I = 5,
  CYAN_I    = 6,
  WHITE_I   = 7,
  
  // Bold ANSI colours
  BOLD_BLACK_I   = 8,
  BOLD_RED_I     = 9,
  BOLD_GREEN_I   = 10,
  BOLD_YELLOW_I  = 11,
  BOLD_BLUE_I    = 12,
  BOLD_MAGENTA_I = 13,
  BOLD_CYAN_I    = 14,
  BOLD_WHITE_I   = 15,

  // Colour numbers 16 through 231 are occupied by a 6x6x6 colour cube,
  // with R at most significant and B at least. (36*R + 6*G + B + 16)
  
  // Colour numbers 232 through 255 are occupied by a uniform series of
  // gray shades running between black and white but not including either
  // on grounds of redundancy.

  // Default foreground
  FG_COLOUR_I      = 256,
  BOLD_FG_COLOUR_I = 257,
  
  // Default background
  BG_COLOUR_I      = 258,
  BOLD_BG_COLOUR_I = 259,
  
  // Cursor colours
  CURSOR_TEXT_COLOUR_I = 260,
  CURSOR_COLOUR_I      = 261,
  IME_CURSOR_COLOUR_I  = 262,

  // Number of colours
  COLOUR_NUM = 263

} colour_i;


/*
 * UCSWIDE is a special value used in the terminal data to signify
 * the character cell containing the right-hand half of a CJK wide
 * character.
 */
enum { UCSWIDE = 0 };

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
enum {
  ATTR_FGSHIFT   = 0,
  ATTR_BGSHIFT   = 9,
  ATTR_FGMASK    = 0x00001FFu,
  ATTR_BGMASK    = 0x003FE00u,
  ATTR_INVALID   = 0x003FFFFu,
  ATTR_BOLD      = 0x0040000u,
  ATTR_DIM       = 0x0080000u,
  ATTR_INVISIBLE = 0x0100000u,
  ATTR_UNDER     = 0x0200000u,
  ATTR_REVERSE   = 0x0400000u,
  ATTR_BLINK     = 0x0800000u,
  ATTR_PROTECTED = 0x1000000u,
  ATTR_WIDE      = 0x2000000u,
  ATTR_NARROW    = 0x4000000u,

  TATTR_RIGHTCURS = 0x10000000u, /* cursor-on-RHS */
  TATTR_PASCURS   = 0x20000000u, /* passive cursor (box) */
  TATTR_ACTCURS   = 0x40000000u, /* active cursor (block) */
  TATTR_COMBINING = 0x80000000u, /* combining characters */

  DATTR_STARTRUN  = 0x80000000u, /* start of redraw run */
  DATTR_MASK      = 0xF0000000u,

  LATTR_NORM     = 0x00000000u,
  LATTR_WIDE     = 0x00000001u,
  LATTR_TOP      = 0x00000002u,
  LATTR_BOT      = 0x00000003u,
  LATTR_MODE     = 0x00000003u,
  LATTR_WRAPPED  = 0x00000010u, /* this line wraps to next */
  LATTR_WRAPPED2 = 0x00000020u, /* with WRAPPED: CJK wide character
                                 * wrapped to next line, so last
                                 * single-width cell is empty */
};

enum {
  ATTR_DEFFG = FG_COLOUR_I << ATTR_FGSHIFT,
  ATTR_DEFBG = BG_COLOUR_I << ATTR_BGSHIFT,
  ATTR_DEFAULT = ATTR_DEFFG | ATTR_DEFBG,
};


typedef struct {
 /*
  * The cc_next field is used to link multiple termchars
  * together into a list, so as to fit more than one character
  * into a character cell (Unicode combining characters).
  * 
  * cc_next is a relative offset into the current array of
  * termchars. I.e. to advance to the next character in a list,
  * one does `tc += tc->next'.
  * 
  * Zero means end of list.
  */
  short cc_next;

 /*
  * Any code in terminal.c which definitely needs to be changed
  * when extra fields are added here is labelled with a comment
  * saying FULL-TERMCHAR.
  */
  wchar chr;
  uint attr;

} termchar;

const termchar basic_erase_char;

typedef struct {
  ushort attr;
  ushort cols;    /* number of real columns on the line */
  ushort size;    /* number of allocated termchars
                     (cc-lists may make this > cols) */
  bool temporary; /* true if decompressed from scrollback */
  short cc_free;  /* offset to first cc in free list */
  termchar *chars;
} termline;

typedef termline *termlines;

typedef struct {
  int width;
  termchar *chars;
  int *forward, *backward;      /* the permutations of line positions */
} bidi_cache_entry;

termline *newline(int cols, int bce);
void freeline(termline *);
void clearline(termline *);
void resizeline(termline *, int);

int sblines(void);
termline *fetch_line(int y);
void release_line(termline *);

int termchars_equal(termchar *a, termchar *b);
int termchars_equal_override(termchar *a, termchar *b, uint bchr, uint battr);

void copy_termchar(termline *destline, int x, termchar *src);
void move_termchar(termline *line, termchar *dest, termchar *src);

void add_cc(termline *, int col, wchar chr);
void clear_cc(termline *, int col);

uchar *compressline(termline *);
termline *decompressline(uchar *, int *bytes_used);

termchar *term_bidi_line(termline *, int scr_y);

/* Traditional terminal character sets */
typedef enum {
  CSET_ASCII = 'B',   /* Normal ASCII charset */
  CSET_GBCHR = 'A',   /* UK variant */
  CSET_LINEDRW = '0', /* Line drawing charset */
  CSET_OEM = 'U'      /* OEM Codepage 437 */
} term_cset;

typedef struct {
  int y, x;
} pos;

typedef enum {
  MBT_LEFT = 1, MBT_MIDDLE = 2, MBT_RIGHT = 3
} mouse_button;

typedef struct belltime {
  struct belltime *next;
  uint ticks;
} belltime;

typedef struct {
  short x, y;
  uint attr;
  bool origin;
  bool autowrap;
  bool wrapnext;
  bool utf;
  bool g1;
  term_cset csets[2];
  uchar oem_acs;
} term_cursor;

struct term {
  bool on_alt_screen;     /* On alternate screen? */
  bool show_other_screen;

  termlines *lines, *other_lines;
  term_cursor curs, saved_cursors[2];

  uchar **scrollback;     /* lines scrolled off top of screen */
  int disptop;            /* distance scrolled back (0 or -ve) */
  int sblen;              /* length of scrollback buffer */
  int sblines;            /* number of lines of scrollback */
  int sbpos;              /* index of next scrollback position to be filled */
  int tempsblines;        /* number of lines of .scrollback that
                           * can be retrieved onto the terminal
                           * ("temporary scrollback") */

  termlines *displines;   /* buffer of text on real screen */

  termchar erase_char;

  char *inbuf;      /* terminal input buffer */
  uint inbuf_size, inbuf_pos;

  bool rvideo;   /* global reverse video flag */
  bool cursor_on;        /* cursor enabled flag */
  bool deccolm_allowed;  /* DECCOLM sequence for 80/132 cols allowed? */
  bool reset_132;        /* Flag ESC c resets to 80 cols */
  bool cblinker; /* When blinking is the cursor on ? */
  bool tblinker; /* When the blinking text is on */
  bool blink_is_real;    /* Actually blink blinking text */
  bool echoing;  /* Does terminal want local echo? */
  bool insert;   /* Insert mode */
  int marg_top, marg_bot;  /* scroll margins */
  bool printing, only_printing;  /* Are we doing ANSI printing? */
  int  print_state;      /* state of print-end-sequence scan */
  char *printbuf;        /* buffered data for printer */
  uint printbuf_size, printbuf_pos;

  int  rows, cols;
  bool has_focus;
  bool in_vbell;

  bool vt220_keys;
  bool shortcut_override;
  bool backspace_sends_bs;
  bool escape_sends_fs;
  bool app_escape_key;
  bool app_cursor_keys;
  bool app_keypad;
  bool app_wheel;
  bool wheel_reporting;
  int  modify_other_keys;
  bool newline_mode;
  bool report_focus;
  bool report_ambig_width;
  bool bracketed_paste;
  bool show_scrollbar;

  int  cursor_type;
  int  cursor_blinks;
  bool cursor_invalid;

  uchar esc_mod;  // Modifier character in escape sequences

  uint csi_argc;
  uint csi_argv[32];

  int  cmd_num;        // OSC command number, or -1 for DCS
  char cmd_buf[2048];  // OSC or DCS string buffer and length
  uint cmd_len;

  uchar *tabs;

  enum {
    NORMAL, ESCAPE, CSI_ARGS,
    IGNORE_STRING, CMD_STRING, CMD_ESCAPE,
    OSC_START, OSC_NUM, OSC_PALETTE
  } state;

  // Mouse mode
  enum {
    MM_NONE,
    MM_X10,       // just clicks
    MM_VT200,     // click and release
    MM_BTN_EVENT, // click, release, and drag with button down
    MM_ANY_EVENT  // click, release, and any movement
  } mouse_mode;

  // Mouse encoding
  enum {
    ME_X10,        // CSI M followed by one byte each for event, X and Y
    ME_UTF8,       // Same as X10, but with UTF-8 encoded X and Y (ugly!)
    ME_URXVT_CSI,  // CSI event ; x ; y M
    ME_XTERM_CSI   // CSI > event ; x ; y M/m
  } mouse_enc;

  enum {
    // The state can be zero, one of the mouse buttons or one of the cases here.
    MS_SEL_CHAR = -1, MS_SEL_WORD = -2, MS_SEL_LINE = -3,
    MS_COPYING = -4, MS_PASTING = -5, MS_OPENING = -6
  } mouse_state;

  bool sel_rect, selected;
  pos sel_start, sel_end, sel_anchor;
  
 /* Scroll steps during selection when cursor out of window. */
  int sel_scroll;
  pos sel_pos;

  wchar *paste_buffer;
  int paste_len, paste_pos;

 /* True when we've seen part of a multibyte input char */
  bool in_mb_char;
  
 /* Non-zero when we've seen the first half of a surrogate pair */
  wchar high_surrogate;

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

void term_resize(int, int);
void term_scroll(int, int);
void term_reset(void);
void term_clear_scrollback(void);
void term_mouse_click(mouse_button, mod_keys, pos, int count);
void term_mouse_release(mouse_button, mod_keys, pos);
void term_mouse_move(mod_keys, pos);
void term_mouse_wheel(int delta, int lines_per_notch, mod_keys, pos);
void term_select_all(void);
void term_paint(void);
void term_invalidate(int left, int top, int right, int bottom);
void term_open(void);
void term_copy(void);
void term_paste(wchar *, uint len);
void term_send_paste(void);
void term_cancel_paste(void);
void term_reconfig(void);
void term_flip_screen(void);
void term_reset_screen(void);
void term_write(const char *, uint len);
void term_flush(void);
void term_set_focus(bool has_focus);
int  term_cursor_type(void);
bool term_cursor_blinks(void);
void term_hide_cursor(void);

#endif
