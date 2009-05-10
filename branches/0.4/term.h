#ifndef TERM_H
#define TERM_H

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

int  term_rows(void);
int  term_cols(void);
int  term_which_screen(void);
bool term_app_cursor_keys(void);
bool term_app_keypad(void);
bool term_has_focus(void);
bool term_big_cursor(void);
bool term_selected(void);

bool term_in_mouse_mode(void);
bool term_in_utf(void);
bool term_editing(void);
bool term_echoing(void);
bool term_newline_mode(void);

#endif
