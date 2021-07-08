// termout.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2017-20 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"
#include "winpriv.h"  // colours, win_get_font, win_change_font, win_led, win_set_scrollview

#include "win.h"
#include "appinfo.h"
#include "charset.h"
#include "child.h"
#include "print.h"
#include "sixel.h"
#include "winimg.h"
#include "tek.h"
#include "base64.h"
#include "unicodever.t"

#include <termios.h>
#include <sys/time.h>

#define TERM_CMD_BUF_INC_STEP 128
#define TERM_CMD_BUF_MAX_SIZE (1024 * 1024)

#define SUB_PARS (1 << (sizeof(*term.csi_argv) * 8 - 1))

/* This combines two characters into one value, for the purpose of pairing
 * any modifier byte and the final byte in escape sequences.
 */
#define CPAIR(x, y) ((x) << 8 | (y))

static string primary_da1 = "\e[?1;2c";
static string primary_da2 = "\e[?62;1;2;4;6;9;15;22;29c";
static string primary_da3 = "\e[?63;1;2;4;6;9;15;22;29c";
static string primary_da4 = "\e[?64;1;2;4;6;9;15;21;22;28;29c";
static string primary_da5 = "\e[?65;1;2;4;6;9;15;21;22;28;29c";
/* Registered Extensions to the Character Cell Display Service Class
	1	132 Column Display
	2	Printer Port
	3	ReGIS Display
	4	Sixels Display
	6	Selectively Erasable Characters
	9	National Replacement Character Sets
	15	Technical Character Set
	21	Horizontal Scrolling
	22	Color Text
	28	Rectangular Editing
	29	Text Locator
*/


static bool
term_push_cmd(char c)
{
  uint new_size;

  /* Need 1 more for null byte */
  if (term.cmd_len + 1 < term.cmd_buf_cap) {
    term.cmd_buf[term.cmd_len++] = c;
    term.cmd_buf[term.cmd_len] = 0;
    return true;
  }

  if (term.cmd_buf_cap >= TERM_CMD_BUF_MAX_SIZE) {
    /* Server sends too many cmd characters */
    return false;
  }
  new_size = term.cmd_buf_cap + TERM_CMD_BUF_INC_STEP;
  if (new_size >= TERM_CMD_BUF_MAX_SIZE) {
    // cosmetic limitation (relevant limitation above)
    new_size = TERM_CMD_BUF_MAX_SIZE;
  }
  term.cmd_buf = renewn(term.cmd_buf, new_size);
  term.cmd_buf_cap = new_size;
  term.cmd_buf[term.cmd_len++] = c;
  term.cmd_buf[term.cmd_len] = 0;
  return true;
}

static void
enable_progress(void)
{
  term.lines[term.curs.y]->lattr |= LATTR_PROGRESS;
}

/*
 * Move the cursor to a given position, clipping at boundaries.
 * We may or may not want to clip at the scroll margin: marg_clip is
 * 0 not to,
 * 1 to disallow _passing_ the margins, and
 * 2 to disallow even _being_ outside the margins.
 */
static void
move(int x, int y, int marg_clip)
{
  term_cursor *curs = &term.curs;

  if (marg_clip) {
    if ((curs->y >= term.marg_top || marg_clip == 2) && y < term.marg_top)
      y = term.marg_top;
    if ((curs->y <= term.marg_bot || marg_clip == 2) && y > term.marg_bot)
      y = term.marg_bot;
    if ((curs->x >= term.marg_left || marg_clip == 2) && x < term.marg_left)
      x = term.marg_left;
    if ((curs->x <= term.marg_right || marg_clip == 2) && x > term.marg_right)
      x = term.marg_right;
  }

  if (x < 0)
    x = 0;
  if (x >= term.cols)
    x = term.cols - 1;
  if (y < 0)
    y = 0;
  if (y >= term.rows)
    y = term.rows - 1;

  curs->x = x;
  curs->y = y;
  curs->wrapnext = false;
}

/*
 * Save the cursor and SGR mode.
 */
static void
save_cursor(void)
{
  term.saved_cursors[term.on_alt_screen] = term.curs;
}

/*
 * Restore the cursor and SGR mode.
 */
static void
restore_cursor(void)
{
  term_cursor *curs = &term.curs;
  *curs = term.saved_cursors[term.on_alt_screen];
  term.erase_char.attr = curs->attr;
  term.erase_char.attr.attr &= (ATTR_FGMASK | ATTR_BGMASK);
  term.erase_char.attr.attr |= TATTR_CLEAR;

 /* Make sure the window hasn't shrunk since the save */
  if (curs->x >= term.cols)
    curs->x = term.cols - 1;
  if (curs->y >= term.rows)
    curs->y = term.rows - 1;

 /* In origin mode, make sure the cursor position is within margins */
  if (curs->origin) {
    if (curs->x < term.marg_left)
      curs->x = term.marg_left;
    else if (curs->x > term.marg_right)
      curs->x = term.marg_right;
    if (curs->y < term.marg_top)
      curs->y = term.marg_top;
    else if (curs->y > term.marg_bot)
      curs->y = term.marg_bot;
  }

 /*
  * wrapnext might reset to False 
  * if the x position is no longer at the rightmost edge.
  */
  if (curs->wrapnext && curs->x < term.cols - 1 && curs->x != term.marg_right)
    curs->wrapnext = false;

  term_update_cs();
}

/*
 * Insert or delete characters within the current line.
 * n is +ve if insertion is desired, and -ve for deletion.
 */
static void
insert_char(int n)
{
  if (term.curs.x < term.marg_left || term.curs.x > term.marg_right)
    return;

  bool del = n < 0;
  int m;
  term_cursor *curs = &term.curs;
  termline *line = term.lines[curs->y];
  int cols = min(line->cols, line->size);
  cols = min(cols, term.marg_right + 1);

  n = (n < 0 ? -n : n);
  if (n > cols - curs->x)
    n = cols - curs->x;
  m = cols - curs->x - n;
  term_check_boundary(curs->x, curs->y);
  term_check_boundary(curs->x + m, curs->y);
  if (del) {
    for (int j = 0; j < m; j++)
      move_termchar(line, line->chars + curs->x + j,
                    line->chars + curs->x + j + n);
    while (n--)
      line->chars[curs->x + m++] = term.erase_char;
  }
  else {
    for (int j = m; j--;)
      move_termchar(line, line->chars + curs->x + j + n,
                    line->chars + curs->x + j);
    while (n--)
      line->chars[curs->x + n] = term.erase_char;
  }
}

static int
charwidth(xchar chr)
{
#if HAS_LOCALES
  if (cfg.charwidth % 10)
    return xcwidth(chr);
  else
    if (chr > 0xFFFF)
      return wcswidth((wchar[]){high_surrogate(chr), low_surrogate(chr)}, 2);
    else
      return wcwidth(chr);
#else
  return xcwidth(chr);
#endif
}

static void
attr_rect(cattrflags add, cattrflags sub, cattrflags xor, short y0, short x0, short y1, short x1)
{
  //printf("attr_rect %d,%d..%d,%d +%llX -%llX ^%llX\n", y0, x0, y1, x1, add, sub, xor);
  y0--; x0--; y1--; x1--;

  if (term.curs.origin) {
    y0 += term.marg_top;
    x0 += term.marg_left;
    y1 += term.marg_top;
    x1 += term.marg_left;
  }
  if (y0 < 0)
    y0 = 0;
  if (x0 < 0)
    x0 = 0;
  if (y1 >= term.rows)
    y1 = term.rows - 1;
  if (x1 >= term.cols)
    x1 = term.cols - 1;
  //printf("%d,%d..%d,%d\n", y0, x0, y1, x1);

  for (int y = y0; y <= y1; y++) {
    termline * l = term.lines[y];
    int xl = x0;
    int xr = x1;
    if (!term.attr_rect) {
      if (y != y0)
        xl = term.marg_left;
      if (y != y1)
        xr = term.marg_right;
    }
    for (int x = xl; x <= xr; x++) {
      //printf("attr %d:%d\n", y, x);
      cattrflags ca = l->chars[x].attr.attr;
      ca ^= xor;
      ca &= ~sub;
      ca |= add;
      if (ca != l->chars[x].attr.attr) {
        if (x == xl)
          term_check_boundary(x, y);
        if (x == xr)
          term_check_boundary(x + 1, y);
      }
      l->chars[x].attr.attr = ca;
    }
  }
}

//static void write_char(wchar c, int width);
static void term_do_write(const char *buf, uint len);

static void
fill_rect(xchar chr, cattr attr, bool sel, short y0, short x0, short y1, short x1)
{
  //printf("fill_rect %d,%d..%d,%d\n", y0, x0, y1, x1);
  int width = charwidth(chr);
  if (chr == UCSWIDE || width < 1)
    return;
  wchar low = 0;
  if (chr > 0xFFFF) {
    low = low_surrogate(chr);
    chr = high_surrogate(chr);
  }

  y0--; x0--; y1--; x1--;

  if (term.curs.origin) {
    y0 += term.marg_top;
    x0 += term.marg_left;
    y1 += term.marg_top;
    x1 += term.marg_left;
  }
  if (y0 < 0)
    y0 = 0;
  if (x0 < 0)
    x0 = 0;
  if (y1 >= term.rows)
    y1 = term.rows - 1;
  if (x1 >= term.cols)
    x1 = term.cols - 1;
  //printf("%d,%d..%d,%d\n", y0, x0, y1, x1);

  //printf("gl %d gr %d csets %d %d %d %d /%d sup %d acs %d\n", term.curs.gl, term.curs.gr, term.curs.csets[0], term.curs.csets[1], term.curs.csets[2], term.curs.csets[3], term.curs.cset_single, term.curs.decsupp, term.curs.oem_acs);
  if ((chr > ' ' && chr < 0x80 
       && (term.curs.csets[term.curs.gl] != CSET_ASCII
           ||
           term.curs.cset_single != CSET_ASCII
          )
      )
      ||
      (chr >= 0x80 && chr < 0x100 
       && ((term.curs.gr && term.curs.csets[term.curs.gr] != CSET_ASCII)
           || term.curs.oem_acs
          )
      )
      || (chr >= 0x2580 && chr <= 0x259F)
     )
  {
    term_cursor csav = term.curs;
    term.curs.attr = attr;
#ifdef debug_FRA_special
    // make this code branch visible
    term.curs.attr.attr &= ~ATTR_FGMASK;
    term.curs.attr.attr |= RED_I << ATTR_FGSHIFT;
#endif
    term.curs.width = 1;
    if (!(width < 2 || (cs_ambig_wide && is_ambig(chr))))
      term.curs.attr.attr |= TATTR_CLEAR | TATTR_NARROW;
    term.state = NORMAL;

    char * cbuf = 0;
    if (chr > 0xFF) {
      wchar * wc = (wchar[]){chr, low, 0};
      cbuf = cs__wcstombs(wc);
    }
    for (int y = y0; y <= y1; y++) {
      term.curs.y = y;
      for (int x = x0; x <= x1; x++) {
        term.curs.x = x;
        term.curs.cset_single = csav.cset_single;
        if (chr > 0xFF) {
          //write_char(chr, 1); // would skip NRCS handling in term_do_write
          term_do_write(cbuf, strlen(cbuf));
        }
        else {
          char c = chr;
          term_do_write(&c, 1);
        }
      }
    }
    if (cbuf)
      free(cbuf);

    term.curs = csav;
    term.curs.cset_single = CSET_ASCII;
    return;
  }

  if (width > 1)
    attr.attr |= TATTR_CLEAR | TATTR_NARROW;

  for (int y = y0; y <= y1; y++) {
    termline * l = term.lines[y];
    bool prevprot = true;  // not false!
    for (int x = x0; x <= x1; x++) {
      //printf("fill %d:%d\n", y, x);
      bool prot = sel && l->chars[x].attr.attr & ATTR_PROTECTED;
      if (prot != prevprot) {
        // |P not here, no check
        // |N check
        // NP check only current position
        // PN check
        if (!prot) {  // includes the case x == x0
          // clear previous half of wide char, even if protected
          term_check_boundary(x0, y);
        }
        else if (l->chars[x].chr == UCSWIDE) {
          // clear right half of wide char, even if protected;
          // calling term_check_boundary would overwrite previous fill char
          clear_cc(l, x);
          l->chars[x].chr = ' ';
        }
      }
      // clear wide char on right area border unless protected
      if (!prot && x == x1)
        term_check_boundary(x1 + 1, y);
      prevprot = prot;

      if (!sel || !prot) {
        clear_cc(l, x);
        l->chars[x].chr = chr;
        l->chars[x].attr = attr;
        if (low)
          add_cc(l, x, low, attr);
      }
    }
  }
}

static void
copy_rect(short y0, short x0, short y1, short x1, short y2, short x2)
{
  //printf("copy_rect %d,%d..%d,%d -> %d,%d\n", y0, x0, y1, x1, y2, x2);
  y0--; x0--; y1--; x1--; y2--; x2--;

  if (term.curs.origin) {
    y0 += term.marg_top;
    x0 += term.marg_left;
    y1 += term.marg_top;
    x1 += term.marg_left;
    y2 += term.marg_top;
    x2 += term.marg_left;
  }
  if (y0 < 0)
    y0 = 0;
  if (x0 < 0)
    x0 = 0;
  if (y1 >= term.rows)
    y1 = term.rows - 1;
  if (x1 >= term.cols)
    x1 = term.cols - 1;

  if (y2 < 0)
    y2 = 0;
  if (x2 < 0)
    x2 = 0;
  if (y2 + y1 - y0 >= term.rows)
    y1 = term.rows + y0 - y2 - 1;
  if (x2 + x1 - x0 >= term.cols)
    x1 = term.cols + x0 - x2 - 1;
  //printf("%d,%d..%d,%d -> %d,%d\n", y0, x0, y1, x1, y2, x2);

  bool down = y2 > y0;
  bool left = x2 > x0;
  for (int y = down ? y1 : y0; down ? y >= y0 : y <= y1; down ? y-- : y++) {
    termline * src = term.lines[y];
    termline * dst = term.lines[y + y2 - y0];
    term_check_boundary(x2, y + y2 - y0);
    term_check_boundary(x2 + x1 - x0 + 1, y + y2 - y0);
    for (int x = left ? x1 : x0; left ? x >= x0 : x <= x1; left ? x-- : x++) {
      copy_termchar(dst, x + x2 - x0, &src->chars[x]);
      //printf("copy %d:%d -> %d:%d\n", y, x, y + y2 - y0, x + x2 - x0);
      if ((x == x0 && src->chars[x].chr == UCSWIDE)
       || (x == x1 && charwidth(src->chars[x].chr) != 1)
         )
      {
        clear_cc(dst, x);
        dst->chars[x].chr = ' ';
      }
    }
  }
}

void
scroll_rect(int topline, int botline, int lines)
{
  //printf("scroll_rect %d..%d %s%d\n", topline, botline, lines > 0 ? "+" : "", lines);
  int y0, y1, y2, e0, e1;
  if (lines < 0) {  // downwards
//	scroll		copy		clear
//	4	-2	4	6	4
//	20		18		5
    if (topline - lines > term.marg_bot + 1)
      lines = topline - term.marg_bot - 1;
    y0 = topline;
    y1 = botline + lines;
    y2 = topline - lines;
    e0 = y0;
    e1 = y0 - lines - 1;
  }
  else {
//	scroll		copy		clear
//	4	+2	6	4	19
//	20		20		20
    if (topline + lines > term.marg_bot + 1)
      lines = term.marg_bot + 1 - topline;
    y0 = topline + lines;
    y1 = botline;
    y2 = topline;
    e0 = y1 - lines + 1;
    e1 = y1;
  }
  y0++; y1++; y2++; e0++; e1++;
  int xl = term.marg_left + 1;
  int xr = term.marg_right + 1;
  if (term.curs.origin) {
    // compensate for the originmode applied in the functions called below
    xl = 1;
    xr = term.marg_right - term.marg_left + 1;
    y0 -= term.marg_top;
    y1 -= term.marg_top;
    y2 -= term.marg_top;
    e0 -= term.marg_top;
    e1 -= term.marg_top;
  }
  copy_rect(y0, xl, y1, xr, y2, xl);
  fill_rect(' ', term.curs.attr, false, e0, xl, e1, xr);
}

static void
insdel_column(int col, bool del, int n)
{
  //printf("insdel_column @%d %d marg %d..%d\n", col, n, term.marg_left, term.marg_right);
  int x0, x1, x2, e0, e1;
  if (del) {
    x0 = col + n;
    x1 = term.marg_right;
    x2 = col;
    e0 = term.marg_right - n + 1;
    e1 = term.marg_right;
    if (x0 > term.marg_right) {
      x0 = term.marg_right;
      e0 = col;
    }
  }
  else {
    if (col + n > term.marg_right + 1)
      n = term.marg_right + 1 - col;
    x0 = col;
    x1 = term.marg_right - n;
    x2 = col + n;
    e0 = col;
    e1 = col + n - 1;
  }
  x0++; x1++; x2++; e0++; e1++;
  int yt = term.marg_top + 1;
  int yb = term.marg_bot + 1;
  if (term.curs.origin) {
    // compensate for the originmode applied in the functions called below
    yt = 1;
    yb = term.marg_bot - term.marg_top + 1;
    x0 -= term.marg_left;
    x1 -= term.marg_left;
    x2 -= term.marg_left;
    e0 -= term.marg_left;
    e1 -= term.marg_left;
  }
  copy_rect(yt, x0, yb, x1, yt, x2);
  fill_rect(' ', term.curs.attr, false, yt, e0, yb, e1);
}

static uint
sum_rect(short y0, short x0, short y1, short x1)
{
  //printf("sum_rect %d,%d..%d,%d\n", y0, x0, y1, x1);

  y0--; x0--; y1--; x1--;

  if (term.curs.origin) {
    y0 += term.marg_top;
    x0 += term.marg_left;
    y1 += term.marg_top;
    x1 += term.marg_left;
  }
  if (y0 < 0)
    y0 = 0;
  if (x0 < 0)
    x0 = 0;
  if (y1 >= term.rows)
    y1 = term.rows - 1;
  if (x1 >= term.cols)
    x1 = term.cols - 1;
  //printf("%d,%d..%d,%d\n", y0, x0, y1, x1);

  uint sum = 0;
  for (int y = y0; y <= y1; y++) {
    termline * line = term.lines[y];
    for (int x = x0; x <= x1; x++) {
      //printf("add %d:%d\n", y, x);
      if (line->chars[x].chr == UCSWIDE) {
      }
      else {
        sum += line->chars[x].chr;  // xterm default would mask & 0xFF
        cattrflags attr = line->chars[x].attr.attr;
        if (attr & ATTR_UNDER)
          sum += 0x10;
        else if (attr & ATTR_REVERSE)
          sum += 0x20;
        else if (attr & ATTR_BLINK)
          sum += 0x40;
        else if (attr & ATTR_BOLD)
          sum += 0x80;
        int xc = x;
        while (line->chars[xc].cc_next) {
          xc += line->chars[xc].cc_next;
          sum += line->chars[xc].chr & 0xFF;
        }
      }
    }
  }
  return sum;
}


static void
write_bell(void)
{
  if (cfg.bell_flash)
    term_schedule_vbell(false, 0);
  win_bell(&cfg);
}

static void
write_backspace(void)
{
  term_cursor *curs = &term.curs;
  if (curs->x == term.marg_left && curs->y == term.marg_top
      && term.rev_wrap && !cfg.old_wrapmodes
     )
  {
    curs->y = term.marg_bot;
    curs->x = term.marg_right;
  }
  else if (curs->x == 0 && (curs->y == term.marg_top || !term.autowrap
                       || (!cfg.old_wrapmodes && !term.rev_wrap)))
    /* skip */;
  else if (curs->x == term.marg_left && curs->y > term.marg_top) {
    curs->y--;
    curs->x = term.marg_right;
  }
  else if (curs->wrapnext) {
    curs->wrapnext = false;
    if (!term.rev_wrap && !cfg.old_wrapmodes)
      curs->x--;
  }
  else if (curs->x > 0 && curs->x != term.marg_left)
    curs->x--;
}

static void
write_tab(void)
{
  term_cursor *curs = &term.curs;

  int last = -1;
  do {
    if (curs->x == term.marg_right)
      break;
    last = curs->x;
    if (term.lines[curs->y]->chars[last].chr == ' '
        && (term.lines[curs->y]->chars[last].attr.attr & TATTR_CLEAR)
       )
      term.lines[curs->y]->chars[last].attr.attr |= ATTR_DIM;
    curs->x++;
  } while (curs->x < term.cols - 1 && !term.tabs[curs->x]);
  if (last >= 0
      && term.lines[curs->y]->chars[last].chr == ' '
      && (term.lines[curs->y]->chars[last].attr.attr & TATTR_CLEAR)
     )
    term.lines[curs->y]->chars[last].attr.attr |= ATTR_BOLD;

  if ((term.lines[curs->y]->lattr & LATTR_MODE) != LATTR_NORM) {
    if (curs->x >= term.cols / 2)
      curs->x = term.cols / 2 - 1;
  }
  else {
    if (curs->x >= term.cols)
      curs->x = term.cols - 1;
  }
}

static void
write_return(void)
{
  term.curs.wrapnext = false;
  if (term.curs.x < term.marg_left)
    term.curs.x = 0;
  else
    term.curs.x = term.marg_left;
  enable_progress();
}

static void
write_linefeed(void)
{
  term_cursor *curs = &term.curs;
  if (curs->x < term.marg_left || curs->x > term.marg_right)
    return;

  clear_wrapcontd(term.lines[curs->y], curs->y);
  if (curs->y == term.marg_bot)
    term_do_scroll(term.marg_top, term.marg_bot, 1, true);
  else if (curs->y < term.rows - 1)
    curs->y++;
  curs->wrapnext = false;
}

static void
write_primary_da(void)
{
  string primary_da = primary_da4;
  char * vt = strstr(cfg.term, "vt");
  if (vt) {
    unsigned int ver;
    if (sscanf(vt + 2, "%u", &ver) == 1) {
      if (ver >= 500)
        primary_da = primary_da5;
      else if (ver >= 400)
        primary_da = primary_da4;
      else if (ver >= 300)
        primary_da = primary_da3;
      else if (ver >= 200)
        primary_da = primary_da2;
      else
        primary_da = primary_da1;
    }
  }
  child_write(primary_da, strlen(primary_da));
}

static wchar last_high = 0;
static wchar last_char = 0;
static int last_width = 0;
cattr last_attr = {.attr = ATTR_DEFAULT,
                   .truefg = 0, .truebg = 0, .ulcolr = (colour)-1};

static void
write_char(wchar c, int width)
{
  //if (kb_trace) printf("[%ld] write_char 'q'\n", mtime());

  if (tek_mode) {
    tek_write(c, width);
    return;
  }

  if (!c)
    return;

  term_cursor * curs = &term.curs;
  termline * line = term.lines[curs->y];

  // support non-BMP for the REP function;
  // this is a hack, it would be cleaner to fold the term_write block
  //   switch (term.state) when NORMAL:
  // and repeat that
  if (width == -1) {  // low surrogate
    last_high = last_char;
  }
  else {
    last_high = 0;
    last_width = width;
  }
  last_char = c;
  last_attr = curs->attr;

  void wrapparabidi(ushort parabidi, termline * line, int y)
  {
    line->lattr = (line->lattr & ~LATTR_BIDIMASK) | parabidi | LATTR_WRAPCONTD;

#ifdef determine_parabidi_during_output
    if (parabidi & (LATTR_BIDISEL | LATTR_AUTOSEL))
      return;

    // if direction autodetection pending:
    // from current line, extend backward and forward to adjust 
    // "paragraph" bidi attributes (esp. direction) to wrapped lines
    termline * paraline = line;
    int paray = y;
    while ((paraline->lattr & LATTR_WRAPCONTD) && paray > -sblines()) {
      paraline = fetch_line(--paray);
      paraline->lattr = (paraline->lattr & ~LATTR_BIDIMASK) | parabidi;
      release_line(paraline);
    }
    paraline = line;
    paray = y;
    while ((paraline->lattr & LATTR_WRAPPED) && paray < term.rows) {
      paraline = fetch_line(++paray);
      paraline->lattr = (paraline->lattr & ~LATTR_BIDIMASK) | parabidi;
      release_line(paraline);
    }
#else
    (void)y;
#endif
  }

  void put_char(wchar c)
  {
    if (term.ring_enabled && curs->x == term.marg_right + 1 - 8) {
      win_margin_bell(&cfg);
      term.ring_enabled = false;
    }

    clear_cc(line, curs->x);
    line->chars[curs->x].chr = c;
    line->chars[curs->x].attr = curs->attr;
#ifdef insufficient_approach
#warning this does not help when scrolling via rectangular copy
    if (term.lrmargmode)
      line->lattr &= ~LATTR_MODE;
#endif
    if (!(line->lattr & LATTR_WRAPCONTD))
      line->lattr = (line->lattr & ~LATTR_BIDIMASK) | curs->bidimode;
    //TODO: if changed, propagate mode onto paragraph
    if (cfg.ligatures_support)
      term_invalidate(0, curs->y, curs->x, curs->y);
  }

  if (curs->wrapnext && term.autowrap && width > 0) {
    line->lattr |= LATTR_WRAPPED;
    line->wrappos = curs->x;
    ushort parabidi = getparabidi(line);
    if (curs->y == term.marg_bot)
      term_do_scroll(term.marg_top, term.marg_bot, 1, true);
    else if (curs->y < term.rows - 1)
      curs->y++;
    curs->x = term.marg_left;
    curs->wrapnext = false;
    line = term.lines[curs->y];
    wrapparabidi(parabidi, line, curs->y);
  }

  bool overstrike = false;
  if (curs->attr.attr & ATTR_OVERSTRIKE) {
    width = 0;
    overstrike = true;
    curs->wrapnext = false;
  }

  bool single_width = false;

  // adjust to explicit width attribute; not for combinings and low surrogates
  if (curs->width && width > 0) {
    //if ((c & 0xFFF) == 0x153) printf("%llX %d\n", curs->attr.attr, width);
    if (curs->width == 1) {
      if (!(width < 2 || (cs_ambig_wide && is_ambig(c))))
        curs->attr.attr |= TATTR_CLEAR | TATTR_NARROW;
      width = 1;
    }
    else if (curs->width == 11) {
      if (width > 1) {
        if (!(cs_ambig_wide && is_ambig(c))) {
          single_width = true;
          curs->attr.attr |= TATTR_SINGLE;
        }
        width = 1;
      }
    }
    else if (curs->width == 2) {
      if (width < 2) {
        curs->attr.attr |= TATTR_EXPAND;
        width = 2;
      }
    }
#ifdef support_triple_width
    else if (curs->width == 3) {
      if (width < 2 || (cs_ambig_wide && is_ambig(c)))
        curs->attr.attr |= TATTR_EXPAND;
#define TATTR_TRIPLE 0x0080000000000000u
      curs->attr.attr |= TATTR_TRIPLE;
      width = 3;
    }
#endif
  }

#ifdef enforce_ambiguous_narrow_here
  // enforce ambiguous-narrow as configured or for WSL;
  // this could be done here but is now sufficiently achieved in charset.c
  if (cs_ambig_narrow && width > 1 && is_ambig(c))
    width = 1;
#endif

  if (cfg.charwidth >= 10 || cs_single_forced) {
    if (width > 1) {
      single_width = true;
      width = 1;
    }
    else if (is_wide(c) || (cs_ambig_wide && is_ambig(c))) {
      single_width = true;
    }
  }

  if (term.insert && width > 0)
    insert_char(width);

  switch (width) {
    when 1:  // Normal character.
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + 1, curs->y);
      put_char(c);
      if (single_width)
        line->chars[curs->x].attr.attr |= TATTR_SINGLE;
    when 2 or 3:  // Double-width char (Triple-width was an experimental option).
     /*
      * If we're about to display a double-width character 
      * starting in the rightmost column, 
      * then we do something special instead.
      * We must print a space in the last column of the screen, then wrap;
      * and we also set LATTR_WRAPPED2 which instructs subsequent 
      * cut-and-pasting not only to splice this line to the one after it, 
      * but to ignore the space in the last character position as well.
      * (Because what was actually output to the terminal was presumably 
      * just a sequence of CJK characters, and we don't want a space to be
      * pasted in the middle of those just because they had the misfortune 
      * to start in the wrong parity column. xterm concurs.)
      */
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + width, curs->y);
      if (curs->x == term.marg_right || curs->x == term.cols - 1) {
        line->chars[curs->x] = term.erase_char;
        line->lattr |= LATTR_WRAPPED | LATTR_WRAPPED2;
        line->wrappos = curs->x;
        ushort parabidi = getparabidi(line);
        if (curs->y == term.marg_bot)
          term_do_scroll(term.marg_top, term.marg_bot, 1, true);
        else if (curs->y < term.rows - 1)
          curs->y++;
        curs->x = term.marg_left;
        line = term.lines[curs->y];
        wrapparabidi(parabidi, line, curs->y);
       /* Now we must term_check_boundary again, of course. */
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + width, curs->y);
      }
      put_char(c);
      curs->x++;
      put_char(UCSWIDE);
#ifdef support_triple_width
      if (width > 2) {
        for (int i = 2; i < width; i++) {
          curs->x++;
          put_char(UCSWIDE);
        }
      }
#endif
    when 0 or -1:  // Combining character or Low surrogate.
#ifdef debug_surrogates
      printf("write_char %04X %2d %08llX\n", c, width, curs->attr.attr);
#endif
      if (curs->x > 0 || overstrike) {
       /* If we're in wrapnext state, the character
        * to combine with is _here_, not to our left. */
        int x = curs->x - !curs->wrapnext;
       /* Same if we overstrike an actually not combining character. */
        if (overstrike)
          x = curs->x;
       /*
        * If the previous character is UCSWIDE, back up another one.
        */
        if (line->chars[x].chr == UCSWIDE) {
          assert(x > 0);
          x--;
        }
       /* Try to precompose with the cell's base codepoint */
        wchar pc;
        if (termattrs_equal_fg(&line->chars[x].attr, &curs->attr))
          pc = win_combine_chars(line->chars[x].chr, c, curs->attr.attr);
        else
          pc = 0;
        if (pc)
          line->chars[x].chr = pc;
        else
          add_cc(line, x, c, curs->attr);
      }
      else {
        // add initial combining characters, 
        // particularly to include initial bidi directional markers
        add_cc(line, -1, c, curs->attr);
      }
      if (!overstrike)
        return;
      // otherwise width 0 was faked for this switch, 
      // and we still need to advance the cursor below
    otherwise:  // Anything else. Probably shouldn't get here.
      return;
  }

  curs->x++;
  if (curs->x == term.marg_right + 1 || curs->x == term.cols) {
    curs->x--;
    if (term.autowrap || cfg.old_wrapmodes)
      curs->wrapnext = true;
  }
}

#define dont_debug_scriptfonts

struct rangefont {
  ucschar first, last;
  uchar font;
  char * scriptname;
};
static struct rangefont scriptfonts[] = {
#include "scripts.t"
};
static struct rangefont blockfonts[] = {
#include "blocks.t"
};
static bool scriptfonts_init = false;
static bool use_blockfonts = false;

static void
mapfont(struct rangefont * ranges, uint len, char * script, uchar f)
{
  for (uint i = 0; i < len; i++) {
    if (0 == strcmp(ranges[i].scriptname, script))
      ranges[i].font = f;
  }
  if (0 == strcmp(script, "CJK")) {
    mapfont(ranges, len, "Han", f);
    mapfont(ranges, len, "Hangul", f);
    mapfont(ranges, len, "Katakana", f);
    mapfont(ranges, len, "Hiragana", f);
    mapfont(ranges, len, "Bopomofo", f);
    mapfont(ranges, len, "Kanbun", f);
    mapfont(ranges, len, "Fullwidth", f);
    mapfont(ranges, len, "Halfwidth", f);
  }
}

static char *
cfg_apply(char * conf, char * item)
{
  char * cmdp = conf;
  char sepch = ';';
  if ((uchar)*cmdp <= (uchar)' ')
    sepch = *cmdp++;

  char * paramp;
  while ((paramp = strchr(cmdp, ':'))) {
    *paramp = '\0';
    paramp++;
    char * sepp = strchr(paramp, sepch);
    if (sepp)
      *sepp = '\0';

    if (!item || !strcmp(cmdp, item)) {
      if (*cmdp == '|')
        mapfont(blockfonts, lengthof(blockfonts), cmdp + 1, atoi(paramp));
      else
        mapfont(scriptfonts, lengthof(scriptfonts), cmdp, atoi(paramp));
    }

    if (sepp) {
      cmdp = sepp + 1;
      // check for multi-line separation
      if (*cmdp == '\\' && cmdp[1] == '\n') {
        cmdp += 2;
        while (iswspace(*cmdp))
          cmdp++;
      }
    }
    else
      break;
  }
  return 0;
}

static void
init_scriptfonts(void)
{
  if (*cfg.font_choice) {
    char * cfg_scriptfonts = cs__wcstombs(cfg.font_choice);
    cfg_apply(cfg_scriptfonts, 0);
    free(cfg_scriptfonts);
    use_blockfonts = wcschr(cfg.font_choice, '|');
  }
  scriptfonts_init = true;
}

uchar
scriptfont(ucschar ch)
{
  if (!*cfg.font_choice)
    return 0;
  if (!scriptfonts_init)
    init_scriptfonts();

  int i, j, k;

  if (use_blockfonts) {
    i = -1;
    j = lengthof(blockfonts);
    while (j - i > 1) {
      k = (i + j) / 2;
      if (ch < blockfonts[k].first)
        j = k;
      else if (ch > blockfonts[k].last)
        i = k;
      else {
        uchar f = blockfonts[k].font;
        if (f)
          return f;
        break;
      }
    }
  }

  i = -1;
  j = lengthof(scriptfonts);
  while (j - i > 1) {
    k = (i + j) / 2;
    if (ch < scriptfonts[k].first)
      j = k;
    else if (ch > scriptfonts[k].last)
      i = k;
    else
      return scriptfonts[k].font;
  }
  return 0;
}

static void
write_ucschar(wchar hwc, wchar wc, int width)
{
  cattrflags attr = term.curs.attr.attr;
  ucschar c = hwc ? combine_surrogates(hwc, wc) : wc;
  uchar cf = scriptfont(c);
#ifdef debug_scriptfonts
  if (c && (cf || c > 0xFF))
    printf("write_ucschar %04X scriptfont %d\n", c, cf);
#endif
  if (cf && cf <= 10 && !(attr & FONTFAM_MASK))
    term.curs.attr.attr = attr | ((cattrflags)cf << ATTR_FONTFAM_SHIFT);

  if (hwc) {
    if (width == 1
        && (cfg.charwidth == 10 || cs_single_forced)
        && (is_wide(c) || (cs_ambig_wide && is_ambig(c)))
       )
    { // ensure indication of cjksingle width handling to trigger down-zooming
      width = 2;
    }
    write_char(hwc, width);
    write_char(wc, -1);  // -1 indicates low surrogate
  }
  else
    write_char(wc, width);

  term.curs.attr.attr = attr;
}

static void
write_error(void)
{
  // Write one of REPLACEMENT CHARACTER or, if that does not exist,
  // MEDIUM SHADE which looks appropriately erroneous.
  wchar errch = 0xFFFD;
  win_check_glyphs(&errch, 1, term.curs.attr.attr);
  if (!errch)
    errch = 0x2592;
  write_char(errch, 1);
}


static bool
contains(string s, int i)
{
  while (*s) {
    while (*s == ',' || *s == ' ')
      s++;
    int si = -1;
    int len;
    if (sscanf(s, "%d%n", &si, &len) <= 0)
      return false;
    s += len;
    if (si == i && (!*s || *s == ',' || *s == ' '))
      return true;
  }
  return false;
}


static short prev_state = 0;

static void
tek_gin_fin(void)
{
  if (tek_mode == TEKMODE_GIN)
    tek_mode = TEKMODE_ALPHA;
}

/* Process Tek mode ESC control */
static void
tek_esc(char c)
{
  if (prev_state)
    term.state = prev_state;
  else
    term.state = NORMAL;

  switch (c) {
    when '\e':   /* stay in ESC state */
      term.state = TEK_ESCAPE;
    when '\n':   /* LF: stay in ESC state */
      term.state = TEK_ESCAPE;
    when 0 or '\r':   /* stay in ESC state */
      term.state = TEK_ESCAPE;
    when '\a':   /* BEL: Bell */
      write_bell();
    when '\b' or '\t' or '\v':     /* BS or HT or VT */
      tek_write(c, -2);
    when CTRL('L'):   /* FF: Alpha mode, clear screen */
      tek_mode = TEKMODE_ALPHA;
      term.state = NORMAL;
      tek_bypass = false;
      tek_clear();
    when CTRL('E'):   /* ENQ: terminal type query */
      tek_bypass = true;
      tek_enq();
    when CTRL('N'):   /* LS1: Locking-shift one */
      tek_alt(true);
    when CTRL('O'):   /* LS0: Locking-shift zero */
      tek_alt(false);
    when CTRL('W'):   /* ETB: Make Copy */
      term_save_image();
      tek_bypass = false;
      tek_gin_fin();
    when CTRL('X'):   /* CAN: Set Bypass */
      tek_bypass = true;
    when CTRL('Z'):   /* SUB: Gin mode */
      tek_gin();
      tek_mode = TEKMODE_GIN;
      term.state = NORMAL;
      tek_bypass = true;
    when 0x1C:   /* FS: Special Plot mode */
      tek_mode = TEKMODE_SPECIAL_PLOT;
      term.state = TEK_ADDRESS0;
    when 0x1D:   /* GS: Graph mode */
      tek_mode = TEKMODE_GRAPH0;
      term.state = TEK_ADDRESS0;
    when 0x1E:   /* RS: Incremental Plot mode */
      tek_mode = TEKMODE_INCREMENTAL_PLOT;
      term.state = TEK_INCREMENTAL;
    when 0x1F:   /* US: Normal mode */
      tek_mode = TEKMODE_ALPHA;
      term.state = NORMAL;
    when '`' ... 'g':  /* Normal mode */
      tek_beam(false, false, c & 7);
    when 'h' ... 'o':  /* Defocused mode */
      tek_beam(true, false, c & 7);
    when 'p' ... 'w':  /* Write-Thru mode */
      tek_beam(false, true, c & 7);
    when '8' ... ';':
      tek_font(c - '8');
    when '?':
      if (term.state == TEK_ADDRESS0 || term.state == TEK_ADDRESS)
        term_do_write("", 1);
    when CTRL('C'):
      tek_mode = TEKMODE_OFF;
      term.state = NORMAL;
      win_invalidate_all(false);
    when ']':  /* OSC: operating system command */
      term.state = OSC_START;
  }
}

/* Process Tek mode control character */
static void
tek_ctrl(char c)
{
  if (term.state == TEK_ADDRESS0 || term.state == TEK_ADDRESS)
    prev_state = term.state;

  switch (c) {
    when '\e':   /* ESC: Escape */
      prev_state = term.state;
      term.state = TEK_ESCAPE;
    when '\a':   /* BEL: Bell */
      write_bell();
      tek_bypass = false;
      tek_gin_fin();
    when '\b' or '\t' or '\v':     /* BS or HT or VT */
      if (tek_mode == TEKMODE_ALPHA)
        tek_write(c, -2);
    when '\n':   /* LF: Line feed */
      tek_bypass = false;
      tek_write(c, -2);
      tek_gin_fin();
    when '\r':   /* CR: Carriage return */
      tek_mode = TEKMODE_ALPHA;
      term.state = NORMAL;
      tek_bypass = false;
      tek_write(c, -2);
    when CTRL('O'):   /* SI */
      tek_gin_fin();
    when 0x1C:   /* FS: Point Plot mode */
      tek_mode = TEKMODE_POINT_PLOT;
      term.state = TEK_ADDRESS0;
    when 0x1D:   /* GS: Graph mode */
      tek_mode = TEKMODE_GRAPH0;
      term.state = TEK_ADDRESS0;
    when 0x1E:   /* RS: Incremental Plot mode */
      tek_mode = TEKMODE_INCREMENTAL_PLOT;
      term.state = TEK_INCREMENTAL;
    when 0x1F:   /* US: Normal mode */
      tek_mode = TEKMODE_ALPHA;
      term.state = NORMAL;
      tek_bypass = false;
  }
}

/* Process control character, returning whether it has been recognised. */
static bool
do_ctrl(char c)
{
  if (tek_mode) {
    tek_ctrl(c);
    return true;
  }

  switch (c) {
    when '\e':   /* ESC: Escape */
      term.state = ESCAPE;
      term.esc_mod = 0;
      return true;  // keep preceding char for REP
    when '\a':   /* BEL: Bell */
      write_bell();
    when '\b':     /* BS: Back space */
      write_backspace();
    when '\t':     /* HT: Character tabulation */
      write_tab();
    when '\v':   /* VT: Line tabulation */
      write_linefeed();
      if (term.newline_mode)
        write_return();
    when '\f':   /* FF: Form feed */
      write_linefeed();
      if (term.newline_mode)
        write_return();
    when '\r':   /* CR: Carriage return */
      write_return();
    when '\n':   /* LF: Line feed */
      write_linefeed();
      if (term.newline_mode)
        write_return();
    when CTRL('E'):   /* ENQ: terminal type query */
      if (!term.vt52_mode) {
        char * ab = cs__wcstombs(cfg.answerback);
        child_write(ab, strlen(ab));
        free(ab);
      }
    when CTRL('N'):   /* LS1: Locking-shift one */
      if (!term.vt52_mode) {
        term.curs.gl = 1;
        term_update_cs();
      }
    when CTRL('O'):   /* LS0: Locking-shift zero */
      if (!term.vt52_mode) {
        term.curs.gl = 0;
        term_update_cs();
      }
    otherwise:
      return false;
  }
  last_char = 0;  // cancel preceding char for REP
  return true;
}

static void
do_vt52(uchar c)
{
  term_cursor *curs = &term.curs;
  term.state = NORMAL;
  term.autowrap = false;
  term.rev_wrap = false;
  term.esc_mod = 0;
  switch (c) {
    when '\e':
      term.state = ESCAPE;
    when '<':  /* Exit VT52 mode (Enter VT100 mode). */
      term.vt52_mode = 0;
    when '=':  /* Enter alternate keypad mode. */
      term.app_keypad = true;
    when '>':  /* Exit alternate keypad mode. */
      term.app_keypad = false;
    when 'A':  /* Cursor up. */
      move(curs->x, curs->y - 1, 0);
    when 'B':  /* Cursor down. */
      move(curs->x, curs->y + 1, 0);
    when 'C':  /* Cursor right. */
      move(curs->x + 1, curs->y, 0);
    when 'D':  /* Cursor left. */
      move(curs->x - 1, curs->y, 0);
    when 'F':  /* Enter graphics mode. */
      term.vt52_mode = 2;
    when 'G':  /* Exit graphics mode. */
      term.vt52_mode = 1;
    when 'H':  /* Move the cursor to the home position. */
      move(0, 0, 0);
    when 'I':  /* Reverse line feed. */
      if (curs->y == term.marg_top)
        term_do_scroll(term.marg_top, term.marg_bot, -1, true);
      else if (curs->y > 0)
        curs->y--;
      curs->wrapnext = false;
    when 'J':  /* Erase from the cursor to the end of the screen. */
      term_erase(false, false, false, true);
    when 'K':  /* Erase from the cursor to the end of the line. */
      term_erase(false, true, false, true);
    when 'Y':  /* Move the cursor to given row and column. */
      term.state = VT52_Y;
    when 'Z':  /* Identify. */
      child_write("\e/Z", 3);
    // Atari ST extensions
    when 'E':  /* Clear screen */
      move(0, 0, 0);
      term_erase(false, false, false, true);
    when 'b':  /* Foreground color */
      term.state = VT52_FG;
    when 'c':  /* Background color */
      term.state = VT52_BG;
    when 'd':  /* Clear to start of screen */
      term_erase(false, false, true, false);
    when 'e':  /* Enable cursor */
      term.cursor_on = true;
    when 'f':  /* Disable cursor */
      term.cursor_on = false;
    when 'j':  /* Save cursor */
      save_cursor();
    when 'k':  /* Restore cursor */
      restore_cursor();
    when 'l':  /* Clear line */
      term_erase(false, true, true, true);
      write_return();
    when 'o':  /* Clear to start of line */
      term_erase(false, true, true, false);
    when 'p':  /* Reverse video */
      term.curs.attr.attr |= ATTR_REVERSE;
    when 'q':  /* Normal video */
      term.curs.attr.attr &= ~ATTR_REVERSE;
    when 'v':  /* Wrap on */
      term.autowrap = true;
      term.curs.wrapnext = false;
    when 'w':  /* Wrap off */
      term.autowrap = false;
      term.curs.wrapnext = false;
  }
}

static void
do_vt52_move(void)
{
  term.state = NORMAL;
  uchar y = term.cmd_buf[0];
  uchar x = term.cmd_buf[1];
  if (y < ' ' || x < ' ')
    return;
  move(x - ' ', y - ' ', 0);
}

static void
do_vt52_colour(bool fg, uchar c)
{
  term.state = NORMAL;
  if (fg) {
    term.curs.attr.attr &= ~ATTR_FGMASK;
    term.curs.attr.attr |= ((c & 0xF) + ANSI0) << ATTR_FGSHIFT;
  }
  else {
    term.curs.attr.attr &= ~ATTR_BGMASK;
    term.curs.attr.attr |= ((c & 0xF) + ANSI0) << ATTR_BGSHIFT;
  }
}

static term_cset
lookup_cset(ushort nrc_code, uchar csmask, bool enabled)
{
  static struct {
    ushort design;
    uchar cstype;  // 1: 94-character set, 2: 96-character set, 3: both
    bool free;     // does not need NRC enabling
    uchar cs;
  } csdesignations[] = {
    {'B', 1, 1, CSET_ASCII},	// ASCII
    {'A', 3, 1, CSET_GBCHR},	// UK Latin-1
    {'0', 1, 1, CSET_LINEDRW},	// DEC Special Line Drawing
    {'>', 1, 1, CSET_TECH},		// DEC Technical
    {'U', 1, 1, CSET_OEM},		// OEM Codepage 437
    {'<', 1, 1, CSET_DECSUPP},	// DEC User-preferred Supplemental (VT200)
    {CPAIR('%', '5'), 1, 1, CSET_DECSPGR},	// DEC Supplementary (VT300)
    // definitions for NRC support:
    {'4', 1, 0, CSET_NL},	// Dutch
    {'C', 1, 0, CSET_FI},	// Finnish
    {'5', 1, 0, CSET_FI},	// Finnish
    {'R', 1, 0, CSET_FR},	// French
    {'f', 1, 0, CSET_FR},	// French
    {'Q', 1, 0, CSET_CA},	// French Canadian (VT200, VT300)
    {'9', 1, 0, CSET_CA},	// French Canadian (VT200, VT300)
    {'K', 1, 0, CSET_DE},	// German
    {'Y', 1, 0, CSET_IT},	// Italian
    {'`', 1, 0, CSET_NO},	// Norwegian/Danish
    {'E', 1, 0, CSET_NO},	// Norwegian/Danish
    {'6', 1, 0, CSET_NO},	// Norwegian/Danish
    {CPAIR('%', '6'), 1, 0, CSET_PT},	// Portuguese (VT300)
    {'Z', 1, 0, CSET_ES},	// Spanish
    {'H', 1, 0, CSET_SE},	// Swedish
    {'7', 1, 0, CSET_SE},	// Swedish
    {'=', 1, 0, CSET_CH},	// Swiss
    // 96-character sets (xterm 336)
    {'L', 2, 1, CSET_ISO_Latin_Cyrillic},
    {'F', 2, 1, CSET_ISO_Greek_Supp},
    {'H', 2, 1, CSET_ISO_Hebrew},
    {'M', 2, 1, CSET_ISO_Latin_5},
    {CPAIR('"', '?'), 1, 1, CSET_DEC_Greek_Supp},
    {CPAIR('"', '4'), 1, 1, CSET_DEC_Hebrew_Supp},
    {CPAIR('%', '0'), 1, 1, CSET_DEC_Turkish_Supp},
    {CPAIR('&', '4'), 1, 1, CSET_DEC_Cyrillic},
    {CPAIR('"', '>'), 1, 0, CSET_NRCS_Greek},
    {CPAIR('%', '='), 1, 0, CSET_NRCS_Hebrew},
    {CPAIR('%', '2'), 1, 0, CSET_NRCS_Turkish},
  };
  for (uint i = 0; i < lengthof(csdesignations); i++)
    if (csdesignations[i].design == nrc_code
        && (csdesignations[i].cstype & csmask)
        && (csdesignations[i].free || enabled)
       )
    {
      return csdesignations[i].cs;
    }
  return 0;
}

// compatible state machine expansion for NCR and DECRQM
static uchar esc_mod0 = 0;
static uchar esc_mod1 = 0;

static void
do_esc(uchar c)
{
  term_cursor *curs = &term.curs;
  term.state = NORMAL;

  // NRC designations
  // representation of NRC sequences at this point:
  //		term.esc_mod esc_mod0 esc_mod1 c
  // ESC)B	29 00 00 42
  // ESC)%5	FF 29 25 35
  // 94-character set designation as G0...G3: ()*+
  // 96-character set designation as G1...G3:  -./
  uchar designator = term.esc_mod == 0xFF ? esc_mod0 : term.esc_mod;
  uchar csmask = 0;
  int gi;
  if (designator) {
    void check_designa(char * designa, uchar cstype) {
      char * csdesigna = strchr(designa, designator);
      if (csdesigna) {
        csmask = cstype;
        gi = csdesigna - designa + cstype - 1;
      }
    }
    check_designa("()*+", 1);  // 94-character set designation?
    check_designa("-./", 2);  // 96-character set designation?
  }
  if (csmask) {
    ushort nrc_code = CPAIR(esc_mod1, c);
    term_cset cs = lookup_cset(nrc_code, csmask, term.decnrc_enabled);
    if (cs) {
      curs->csets[gi] = cs;
      term_update_cs();
      last_char = 0;  // cancel preceding char for REP
      return;
    }
  }

  switch (CPAIR(term.esc_mod, c)) {
    when '[':  /* CSI: control sequence introducer */
      term.state = CSI_ARGS;
      term.csi_argc = 1;
      memset(term.csi_argv, 0, sizeof(term.csi_argv));
      memset(term.csi_argv_defined, 0, sizeof(term.csi_argv_defined));
      term.esc_mod = 0;
      return;  // keep preceding char for REP
    when ']':  /* OSC: operating system command */
      term.state = OSC_START;
    when 'P':  /* DCS: device control string */
      term.state = DCS_START;
    when '^' or '_' or 'X': /* PM, APC, SOS strings to be ignored */
      term.state = IGNORE_STRING;
    when '7':  /* DECSC: save cursor */
      save_cursor();
    when '8':  /* DECRC: restore cursor */
      restore_cursor();
    when '=':  /* DECKPAM: Keypad application mode */
      term.app_keypad = true;
    when '>':  /* DECKPNM: Keypad numeric mode */
      term.app_keypad = false;
    when 'D':  /* IND: exactly equivalent to LF */
      write_linefeed();
    when 'E':  /* NEL: exactly equivalent to CR-LF */
      if (curs->x >= term.marg_left && curs->x <= term.marg_right) {
        write_return();
        write_linefeed();
      }
    when 'M':  /* RI: reverse index - backwards LF */
      if (curs->y == term.marg_top)
        term_do_scroll(term.marg_top, term.marg_bot, -1, true);
      else if (curs->y > 0)
        curs->y--;
      curs->wrapnext = false;
    when 'Z':  /* DECID: terminal type query */
      write_primary_da();
    when 'c':  /* RIS: restore power-on settings */
      winimgs_clear();
      term_reset(true);
      if (term.reset_132) {
        win_set_chars(term.rows, 80);
        term.reset_132 = 0;
      }
    when 'H':  /* HTS: set a tab */
      term.tabs[curs->x] = true;
    when 'l':  /* HP Memory Lock */
      if (curs->y < term.marg_bot)
        term.marg_top = curs->y;
    when 'm':  /* HP Memory Unlock */
      term.marg_top = 0;
    when CPAIR('#', '8'): {  /* DECALN: fills screen with Es :-) */
      term.curs.origin = false;
      term.curs.wrapnext = false;
      term.marg_top = 0;
      term.marg_bot = term.rows - 1;
      term.marg_left = 0;
      term.marg_right = term.cols - 1;
      move(0, 0, 0);
      cattr savattr = term.curs.attr;
      term.curs.attr = CATTR_DEFAULT;
      for (int i = 0; i < term.rows; i++) {
        termline *line = term.lines[i];
        for (int j = 0; j < term.cols; j++) {
          line->chars[j] =
            (termchar) {.cc_next = 0, .chr = 'E', .attr = CATTR_DEFAULT};
        }
        line->lattr = LATTR_NORM;
      }
      term.curs.attr = savattr;
      term.disptop = 0;
    }
    when CPAIR('#', '3'):  /* DECDHL: 2*height, top */
      if (!term.lrmargmode) {
        term.lines[curs->y]->lattr &= LATTR_BIDIMASK;
        term.lines[curs->y]->lattr |= LATTR_TOP;
      }
    when CPAIR('#', '4'):  /* DECDHL: 2*height, bottom */
      if (!term.lrmargmode) {
        term.lines[curs->y]->lattr &= LATTR_BIDIMASK;
        term.lines[curs->y]->lattr |= LATTR_BOT;
      }
    when CPAIR('#', '5'):  /* DECSWL: normal */
      term.lines[curs->y]->lattr &= LATTR_BIDIMASK;
      term.lines[curs->y]->lattr |= LATTR_NORM;
    when CPAIR('#', '6'):  /* DECDWL: 2*width */
      if (!term.lrmargmode) {
        term.lines[curs->y]->lattr &= LATTR_BIDIMASK;
        term.lines[curs->y]->lattr |= LATTR_WIDE;
      }
    when CPAIR('%', '8') or CPAIR('%', 'G'):
      curs->utf = true;
      term_update_cs();
    when CPAIR('%', '@'):
      curs->utf = false;
      term_update_cs();
    when 'n':  /* LS2: Invoke G2 character set as GL */
      term.curs.gl = 2;
      term_update_cs();
    when 'o':  /* LS3: Invoke G3 character set as GL */
      term.curs.gl = 3;
      term_update_cs();
    when '~':  /* LS1R: Invoke G1 character set as GR */
      term.curs.gr = 1;
      term_update_cs();
    when '}':  /* LS2R: Invoke G2 character set as GR */
      term.curs.gr = 2;
      term_update_cs();
    when '|':  /* LS3R: Invoke G3 character set as GR */
      term.curs.gr = 3;
      term_update_cs();
    when 'N':  /* SS2: Single Shift G2 character set */
      term.curs.cset_single = curs->csets[2];
    when 'O':  /* SS3: Single Shift G3 character set */
      term.curs.cset_single = curs->csets[3];
    when '6':  /* Back Index (DECBI), VT420 */
      if (curs->x == term.marg_left)
        insdel_column(term.marg_left, false, 1);
      else
        move(curs->x - 1, curs->y, 1);
    when '9':  /* Forward Index (DECFI), VT420 */
      if (curs->x == term.marg_right)
        insdel_column(term.marg_left, true, 1);
      else
        move(curs->x + 1, curs->y, 1);
    when 'V':  /* Start of Guarded Area (SPA) */
      term.curs.attr.attr |= ATTR_PROTECTED;
      term.iso_guarded_area = true;
    when 'W':  /* End of Guarded Area (EPA) */
      term.curs.attr.attr &= ~ATTR_PROTECTED;
      term.iso_guarded_area = true;
  }
  last_char = 0;  // cancel preceding char for REP
}

static void
do_sgr(void)
{
 /* Set Graphics Rendition. */
  uint argc = term.csi_argc;
  cattr attr = term.curs.attr;
  uint prot = attr.attr & ATTR_PROTECTED;
  for (uint i = 0; i < argc; i++) {
    // support colon-separated sub parameters as specified in
    // ISO/IEC 8613-6 (ITU Recommendation T.416)
    int sub_pars = 0;
    // count sub parameters and clear their SUB_PARS flag 
    // (the last one does not have it)
    // but not the SUB_PARS flag of the main parameter
    if (term.csi_argv[i] & SUB_PARS)
      for (uint j = i + 1; j < argc; j++) {
        sub_pars++;
        if (term.csi_argv[j] & SUB_PARS)
          term.csi_argv[j] &= ~SUB_PARS;
        else
          break;
      }
    if (*cfg.suppress_sgr
        && contains(cfg.suppress_sgr, term.csi_argv[i] & ~SUB_PARS))
    {
      // skip suppressed attribute (but keep processing sub_pars)
      // but turn some sequences into virtual sub-parameters
      // in order to get properly adjusted
      if (term.csi_argv[i] == 38 || term.csi_argv[i] == 48) {
        if (i + 2 < argc && term.csi_argv[i + 1] == 5)
          sub_pars = 2;
        else if (i + 4 < argc && term.csi_argv[i + 1] == 2)
          sub_pars = 4;
      }
    }
    else
    switch (term.csi_argv[i]) {
      when 0:
        attr = CATTR_DEFAULT;
        attr.attr |= prot;
      when 1: attr.attr |= ATTR_BOLD;
      when 2: attr.attr |= ATTR_DIM;
      when 1 | SUB_PARS:
        if (i + 1 < argc)
          switch (term.csi_argv[i + 1]) {
            when 2:
              attr.attr |= ATTR_SHADOW;
          }
      when 3: attr.attr |= ATTR_ITALIC;
      when 4:
        attr.attr &= ~UNDER_MASK;
        attr.attr |= ATTR_UNDER;
      when 4 | SUB_PARS:
        if (i + 1 < argc)
          switch (term.csi_argv[i + 1]) {
            when 0:
              attr.attr &= ~UNDER_MASK;
            when 1:
              attr.attr &= ~UNDER_MASK;
              attr.attr |= ATTR_UNDER;
            when 2:
              attr.attr &= ~UNDER_MASK;
              attr.attr |= ATTR_DOUBLYUND;
            when 3:
              attr.attr &= ~UNDER_MASK;
              attr.attr |= ATTR_CURLYUND;
            when 4:
              attr.attr &= ~UNDER_MASK;
              attr.attr |= ATTR_BROKENUND;
            when 5:
              attr.attr &= ~UNDER_MASK;
              attr.attr |= ATTR_BROKENUND | ATTR_DOUBLYUND;
          }
      when 5: attr.attr |= ATTR_BLINK;
      when 6: attr.attr |= ATTR_BLINK2;
      when 7: attr.attr |= ATTR_REVERSE;
      when 8: attr.attr |= ATTR_INVISIBLE;
      when 8 | SUB_PARS:
        if (i + 1 < argc)
          switch (term.csi_argv[i + 1]) {
            when 7:
              attr.attr |= ATTR_OVERSTRIKE;
          }
      when 9: attr.attr |= ATTR_STRIKEOUT;
      when 73: attr.attr |= ATTR_SUPERSCR;
      when 74: attr.attr |= ATTR_SUBSCR;
      when 75: attr.attr &= ~(ATTR_SUPERSCR | ATTR_SUBSCR);
      when 10 ... 11: {  // ... 12 disabled
        // mode 10 is the configured character set
        // mode 11 is the VGA character set (CP437 + control range graphics)
        // mode 12 (VT520, Linux console, not cygwin console) 
        // clones VGA characters into the ASCII range; disabled;
        // modes 11 (and 12) are overridden by alternative font if configured
          uchar arg_10 = term.csi_argv[i] - 10;
          if (arg_10 && *cfg.fontfams[arg_10].name) {
            attr.attr &= ~FONTFAM_MASK;
            attr.attr |= (cattrflags)arg_10 << ATTR_FONTFAM_SHIFT;
          }
          else {
            if (!arg_10)
              attr.attr &= ~FONTFAM_MASK;
            term.curs.oem_acs = arg_10;
            term_update_cs();
          }
        }
      when 12 ... 20:
        attr.attr &= ~FONTFAM_MASK;
        attr.attr |= (cattrflags)(term.csi_argv[i] - 10) << ATTR_FONTFAM_SHIFT;
      //when 21: attr.attr &= ~ATTR_BOLD;
      when 21:
        attr.attr &= ~UNDER_MASK;
        attr.attr |= ATTR_DOUBLYUND;
      when 22: attr.attr &= ~(ATTR_BOLD | ATTR_DIM | ATTR_SHADOW);
      when 23:
        attr.attr &= ~ATTR_ITALIC;
        if (((attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT) + 10 == 20)
          attr.attr &= ~FONTFAM_MASK;
      when 24: attr.attr &= ~UNDER_MASK;
      when 25: attr.attr &= ~(ATTR_BLINK | ATTR_BLINK2);
      when 27: attr.attr &= ~ATTR_REVERSE;
      when 28: attr.attr &= ~(ATTR_INVISIBLE | ATTR_OVERSTRIKE);
      when 29: attr.attr &= ~ATTR_STRIKEOUT;
      when 30 ... 37: /* foreground */
        attr.attr &= ~ATTR_FGMASK;
        attr.attr |= (term.csi_argv[i] - 30 + ANSI0) << ATTR_FGSHIFT;
      when 51 or 52: /* "framed" or "encircled" */
        attr.attr |= ATTR_FRAMED;
      when 54: /* not framed, not encircled */
        attr.attr &= ~ATTR_FRAMED;
      when 53: attr.attr |= ATTR_OVERL;
      when 55: attr.attr &= ~ATTR_OVERL;
      when 90 ... 97: /* bright foreground */
        attr.attr &= ~ATTR_FGMASK;
        attr.attr |= ((term.csi_argv[i] - 90 + 8 + ANSI0) << ATTR_FGSHIFT);
      when 38: /* palette/true-colour foreground */
        if (i + 2 < argc && term.csi_argv[i + 1] == 5) {
          // set foreground to palette colour
          attr.attr &= ~ATTR_FGMASK;
          attr.attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_FGSHIFT);
          i += 2;
        }
        else if (i + 4 < argc && term.csi_argv[i + 1] == 2) {
          // set foreground to RGB
          attr.attr &= ~ATTR_FGMASK;
          attr.attr |= TRUE_COLOUR << ATTR_FGSHIFT;
          uint r = term.csi_argv[i + 2];
          uint g = term.csi_argv[i + 3];
          uint b = term.csi_argv[i + 4];
          attr.truefg = make_colour(r, g, b);
          i += 4;
        }
      when 38 | SUB_PARS: /* ISO/IEC 8613-6 foreground colour */
        if (sub_pars >= 2 && term.csi_argv[i + 1] == 5) {
          // set foreground to palette colour
          attr.attr &= ~ATTR_FGMASK;
          attr.attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_FGSHIFT);
        }
        else if (sub_pars >= 4 && term.csi_argv[i + 1] == 2) {
          // set foreground to RGB
          uint pi = sub_pars >= 5;
          attr.attr &= ~ATTR_FGMASK;
          attr.attr |= TRUE_COLOUR << ATTR_FGSHIFT;
          uint r = term.csi_argv[i + pi + 2];
          uint g = term.csi_argv[i + pi + 3];
          uint b = term.csi_argv[i + pi + 4];
          attr.truefg = make_colour(r, g, b);
        }
        else if ((sub_pars >= 5 && term.csi_argv[i + 1] == 3) ||
                 (sub_pars >= 6 && term.csi_argv[i + 1] == 4)) {
          // set foreground to CMY(K)
          ulong f = term.csi_argv[i + 2];
          ulong c = term.csi_argv[i + 3];
          ulong m = term.csi_argv[i + 4];
          ulong y = term.csi_argv[i + 5];
          ulong k = term.csi_argv[i + 1] == 4 ? term.csi_argv[i + 6] : 0;
          if (c <= f && m <= f && y <= f && k <= f) {
            uint r = (f - c) * (f - k) / f * 255 / f;
            uint g = (f - m) * (f - k) / f * 255 / f;
            uint b = (f - y) * (f - k) / f * 255 / f;
            attr.attr &= ~ATTR_FGMASK;
            attr.attr |= TRUE_COLOUR << ATTR_FGSHIFT;
            attr.truefg = make_colour(r, g, b);
          }
        }
      when 39: /* default foreground */
        attr.attr &= ~ATTR_FGMASK;
        attr.attr |= ATTR_DEFFG;
      when 40 ... 47: /* background */
        attr.attr &= ~ATTR_BGMASK;
        attr.attr |= (term.csi_argv[i] - 40 + ANSI0) << ATTR_BGSHIFT;
      when 100 ... 107: /* bright background */
        attr.attr &= ~ATTR_BGMASK;
        attr.attr |= ((term.csi_argv[i] - 100 + 8 + ANSI0) << ATTR_BGSHIFT);
      when 48: /* palette/true-colour background */
        if (i + 2 < argc && term.csi_argv[i + 1] == 5) {
          // set background to palette colour
          attr.attr &= ~ATTR_BGMASK;
          attr.attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_BGSHIFT);
          i += 2;
        }
        else if (i + 4 < argc && term.csi_argv[i + 1] == 2) {
          // set background to RGB
          attr.attr &= ~ATTR_BGMASK;
          attr.attr |= TRUE_COLOUR << ATTR_BGSHIFT;
          uint r = term.csi_argv[i + 2];
          uint g = term.csi_argv[i + 3];
          uint b = term.csi_argv[i + 4];
          attr.truebg = make_colour(r, g, b);
          i += 4;
        }
      when 48 | SUB_PARS: /* ISO/IEC 8613-6 background colour */
        if (sub_pars >= 2 && term.csi_argv[i + 1] == 5) {
          // set background to palette colour
          attr.attr &= ~ATTR_BGMASK;
          attr.attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_BGSHIFT);
        }
        else if (sub_pars >= 4 && term.csi_argv[i + 1] == 2) {
          // set background to RGB
          uint pi = sub_pars >= 5;
          attr.attr &= ~ATTR_BGMASK;
          attr.attr |= TRUE_COLOUR << ATTR_BGSHIFT;
          uint r = term.csi_argv[i + pi + 2];
          uint g = term.csi_argv[i + pi + 3];
          uint b = term.csi_argv[i + pi + 4];
          attr.truebg = make_colour(r, g, b);
        }
        else if ((sub_pars >= 5 && term.csi_argv[i + 1] == 3) ||
                 (sub_pars >= 6 && term.csi_argv[i + 1] == 4)) {
          // set background to CMY(K)
          ulong f = term.csi_argv[i + 2];
          ulong c = term.csi_argv[i + 3];
          ulong m = term.csi_argv[i + 4];
          ulong y = term.csi_argv[i + 5];
          ulong k = term.csi_argv[i + 1] == 4 ? term.csi_argv[i + 6] : 0;
          if (c <= f && m <= f && y <= f && k <= f) {
            uint r = (f - c) * (f - k) / f * 255 / f;
            uint g = (f - m) * (f - k) / f * 255 / f;
            uint b = (f - y) * (f - k) / f * 255 / f;
            attr.attr &= ~ATTR_BGMASK;
            attr.attr |= TRUE_COLOUR << ATTR_BGSHIFT;
            attr.truebg = make_colour(r, g, b);
          }
        }
      when 49: /* default background */
        attr.attr &= ~ATTR_BGMASK;
        attr.attr |= ATTR_DEFBG;
      when 58 | SUB_PARS: /* ISO/IEC 8613-6 format underline colour */
        if (sub_pars >= 2 && term.csi_argv[i + 1] == 5) {
          // set foreground to palette colour
          attr.attr |= ATTR_ULCOLOUR;
          attr.ulcolr = colours[term.csi_argv[i + 2] & 0xFF];
        }
        else if (sub_pars >= 4 && term.csi_argv[i + 1] == 2) {
          // set foreground to RGB
          uint pi = sub_pars >= 5;
          uint r = term.csi_argv[i + pi + 2];
          uint g = term.csi_argv[i + pi + 3];
          uint b = term.csi_argv[i + pi + 4];
          attr.attr |= ATTR_ULCOLOUR;
          attr.ulcolr = make_colour(r, g, b);
        }
        else if ((sub_pars >= 5 && term.csi_argv[i + 1] == 3) ||
                 (sub_pars >= 6 && term.csi_argv[i + 1] == 4)) {
          // set foreground to CMY(K)
          ulong f = term.csi_argv[i + 2];
          ulong c = term.csi_argv[i + 3];
          ulong m = term.csi_argv[i + 4];
          ulong y = term.csi_argv[i + 5];
          ulong k = term.csi_argv[i + 1] == 4 ? term.csi_argv[i + 6] : 0;
          if (c <= f && m <= f && y <= f && k <= f) {
            uint r = (f - c) * (f - k) / f * 255 / f;
            uint g = (f - m) * (f - k) / f * 255 / f;
            uint b = (f - y) * (f - k) / f * 255 / f;
            attr.attr |= ATTR_ULCOLOUR;
            attr.ulcolr = make_colour(r, g, b);
          }
        }
      when 59: /* default underline colour */
        attr.attr &= ~ATTR_ULCOLOUR;
        attr.ulcolr = (colour)-1;
    }
    // skip sub parameters
    i += sub_pars;
  }
  term.curs.attr = attr;
  term.erase_char.attr = attr;
  term.erase_char.attr.attr &= (ATTR_FGMASK | ATTR_BGMASK);
  term.erase_char.attr.attr |= TATTR_CLEAR;
}

/*
 * Set terminal modes in escape arguments to state.
 */
static void
set_modes(bool state)
{
  for (uint i = 0; i < term.csi_argc; i++) {
    uint arg = term.csi_argv[i];
    if (term.esc_mod) { /* DECSET/DECRST: DEC private mode set/reset */
      if (*cfg.suppress_dec && contains(cfg.suppress_dec, arg))
        ; // skip suppressed DECSET/DECRST operation
      else
      switch (arg) {
        when 1:  /* DECCKM: application cursor keys */
          term.app_cursor_keys = state;
        when 66:  /* DECNKM: application keypad */
          term.app_keypad = state;
        when 2:  /* DECANM: VT100/VT52 mode */
          if (state) {
            // Designate USASCII for character sets G0-G3
            for (uint i = 0; i < lengthof(term.curs.csets); i++)
              term.curs.csets[i] = CSET_ASCII;
            term.curs.cset_single = CSET_ASCII;
            term_update_cs();
          }
          else
            term.vt52_mode = 1;
        when 3:  /* DECCOLM: 80/132 columns */
          if (term.deccolm_allowed) {
            term.selected = false;
            win_set_chars(term.rows, state ? 132 : 80);
            term.reset_132 = state;
            term.marg_top = 0;
            term.marg_bot = term.rows - 1;
            term.marg_left = 0;
            term.marg_right = term.cols - 1;
            move(0, 0, 0);
            if (!term.deccolm_noclear)
              term_erase(false, false, true, true);
          }
        when 5:  /* DECSCNM: reverse video */
          if (state != term.rvideo) {
            term.rvideo = state;
            win_invalidate_all(false);
          }
        when 6:  /* DECOM: DEC origin mode */
          term.curs.origin = state;
          if (state)
            move(term.marg_left, term.marg_top, 0);
          else
            move(0, 0, 0);
        when 7:  /* DECAWM: auto wrap */
          term.autowrap = state;
          term.curs.wrapnext = false;
        when 45:  /* xterm: reverse (auto) wraparound */
          term.rev_wrap = state;
          term.curs.wrapnext = false;
        when 8:  /* DECARM: auto key repeat */
          term.auto_repeat = state;
        when 9:  /* X10_MOUSE */
          term.mouse_mode = state ? MM_X10 : 0;
          win_update_mouse();
        when 12: /* AT&T 610 blinking cursor */
          term.cursor_blinkmode = state;
          term.cursor_invalid = true;
          term_schedule_cblink();
        when 25: /* DECTCEM: enable/disable cursor */
          term.cursor_on = state;
          // Should we set term.cursor_invalid or call term_invalidate ?
#ifdef end_suspend_output_by_enabling_cursor
          if (state) {
            term.suspend_update = false;
            do_update();
          }
#endif
        when 30: /* Show/hide scrollbar */
          if (state != term.show_scrollbar) {
            term.show_scrollbar = state;
            win_update_scrollbar(false);
          }
        when 38: /* DECTEK: Enter Tektronix Mode (VT240, VT330) */
          if (state) {
            tek_mode = TEKMODE_ALPHA;
            tek_init(true, cfg.tek_glow);
          }
        when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
          term.deccolm_allowed = state;
        when 95: /* VT510 DECNCSM: DECCOLM does not clear the screen */
          term.deccolm_noclear = state;
        when 42: /* DECNRCM: national replacement character sets */
          term.decnrc_enabled = state;
        when 44: /* turn on margin bell (xterm) */
          term.margin_bell = state;
        when 67: /* DECBKM: backarrow key mode */
          term.backspace_sends_bs = state;
        when 69: /* DECLRMM/VT420 DECVSSM: enable left/right margins DECSLRM */
          term.lrmargmode = state;
          if (state) {
            for (int i = 0; i < term.rows; i++) {
              termline *line = term.lines[i];
              line->lattr = LATTR_NORM;
            }
          }
          else {
            term.marg_left = 0;
            term.marg_right = term.cols - 1;
          }
        when 80: /* DECSDM: SIXEL display mode */
          term.sixel_display = !state;
        when 1000: /* VT200_MOUSE */
          term.mouse_mode = state ? MM_VT200 : 0;
          win_update_mouse();
        when 1002: /* BTN_EVENT_MOUSE */
          term.mouse_mode = state ? MM_BTN_EVENT : 0;
          win_update_mouse();
        when 1003: /* ANY_EVENT_MOUSE */
          term.mouse_mode = state ? MM_ANY_EVENT : 0;
          win_update_mouse();
        when 1004: /* FOCUS_EVENT_MOUSE */
          term.report_focus = state;
        when 1005: /* Xterm's UTF8 encoding for mouse positions */
          term.mouse_enc = state ? ME_UTF8 : 0;
        when 1006: /* Xterm's CSI-style mouse encoding */
          term.mouse_enc = state ? ME_XTERM_CSI : 0;
        when 1016: /* Xterm's CSI-style mouse encoding with pixel resolution */
          term.mouse_enc = state ? ME_PIXEL_CSI : 0;
        when 1015: /* Urxvt's CSI-style mouse encoding */
          term.mouse_enc = state ? ME_URXVT_CSI : 0;
        when 1037:
          term.delete_sends_del = state;
        when 1042:
          term.bell_taskbar = state;
        when 1043:
          term.bell_popup = state;
        when 47: /* alternate screen */
          if (!cfg.disable_alternate_screen) {
            term.selected = false;
            term_switch_screen(state, false);
            term.disptop = 0;
          }
        when 1047:       /* alternate screen */
          if (!cfg.disable_alternate_screen) {
            term.selected = false;
            term_switch_screen(state, true);
            term.disptop = 0;
          }
        when 1046:       /* enable/disable alternate screen switching */
          if (term.on_alt_screen && !state)
            term_switch_screen(false, false);
          cfg.disable_alternate_screen = !state;
        when 1048:       /* save/restore cursor */
          if (!cfg.disable_alternate_screen) {
            if (state)
              save_cursor();
            else
              restore_cursor();
          }
        when 1049:       /* cursor & alternate screen */
          if (!cfg.disable_alternate_screen) {
            if (state)
              save_cursor();
            term.selected = false;
            term_switch_screen(state, true);
            if (!state)
              restore_cursor();
            term.disptop = 0;
          }
        when 1061:       /* VT220 keyboard emulation */
          term.vt220_keys = state;
        when 2004:       /* xterm bracketed paste mode */
          term.bracketed_paste = state;

        /* Mintty private modes */
        when 7700:       /* CJK ambigous width reporting */
          term.report_ambig_width = state;
        when 7711:       /* Scroll marker in current line */
          if (state)
            term.lines[term.curs.y]->lattr |= LATTR_MARKED;
          else
            term.lines[term.curs.y]->lattr |= LATTR_UNMARKED;
        when 7727:       /* Application escape key mode */
          term.app_escape_key = state;
        when 7728:       /* Escape sends FS (instead of ESC) */
          term.escape_sends_fs = state;
        when 7730:       /* Sixel scrolling end position */
          /* on: sixel scrolling moves cursor to beginning of the line
             off(default): sixel scrolling moves cursor to left of graphics */
          term.sixel_scrolls_left = state;
        when 7766:       /* 'B': Show/hide scrollbar (if enabled in config) */
          if (cfg.scrollbar && state != term.show_scrollbar) {
            term.show_scrollbar = state;
            win_update_scrollbar(true);
          }
        when 7767:       /* 'C': Changed font reporting */
          term.report_font_changed = state;
        when 7783:       /* 'S': Shortcut override */
          term.shortcut_override = state;
        when 1007:       /* Alternate Scroll Mode, xterm */
          term.wheel_reporting_xterm = state;
        when 7786:       /* 'V': Mousewheel reporting */
          term.wheel_reporting = state;
        when 7787:       /* 'W': Application mousewheel mode */
          term.app_wheel = state;
        when 7796:       /* Bidi disable in current line */
          if (state)
            term.lines[term.curs.y]->lattr |= LATTR_NOBIDI;
          else
            term.lines[term.curs.y]->lattr &= ~LATTR_NOBIDI;
        when 77096:      /* Bidi disable */
          term.disable_bidi = state;
        when 8452:       /* Sixel scrolling end position right */
          /* on: sixel scrolling leaves cursor to right of graphic
             off(default): position after sixel depends on sixel_scrolls_left */
          term.sixel_scrolls_right = state;
        when 77000 ... 77031: { /* Application control key modes */
          int ctrl = arg - 77000;
          term.app_control = (term.app_control & ~(1 << ctrl)) | (state << ctrl);
        }
        when 2500: /* bidi box graphics mirroring */
          if (state)
            term.curs.bidimode |= LATTR_BOXMIRROR;
          else
            term.curs.bidimode &= ~LATTR_BOXMIRROR;
        when 2501: /* bidi direction auto-detection */
          if (state)
            term.curs.bidimode &= ~LATTR_BIDISEL;
          else
            term.curs.bidimode |= LATTR_BIDISEL;
        when 2026:
          term.suspend_update = state ? 150 : 0;
          if (!state) {
            do_update();
            usleep(1000);  // flush update
          }
      }
    }
    else { /* SM/RM: set/reset mode */
      switch (arg) {
        when 4:  /* IRM: set insert mode */
          term.insert = state;
        when 8: /* BDSM: ECMA-48 bidirectional support mode */
          if (state)
            term.curs.bidimode &= ~LATTR_NOBIDI;
          else
            term.curs.bidimode |= LATTR_NOBIDI;
        when 12: /* SRM: set echo mode */
          term.echoing = !state;
        when 20: /* LNM: Return sends ... */
          term.newline_mode = state;
#ifdef support_Wyse_cursor_modes
        when 33: /* WYSTCURM: steady Wyse cursor */
          term.cursor_blinkmode = !state;
          term.cursor_invalid = true;
          term_schedule_cblink();
        when 34: /* WYULCURM: Wyse underline cursor */
          term.cursor_type = state;
          term.cursor_blinkmode = false;
          term.cursor_invalid = true;
          term_schedule_cblink();
#endif
      }
    }
  }
}

/*
 * Get terminal mode.
            0 - not recognized
            1 - set
            2 - reset
            3 - permanently set
            4 - permanently reset
 */
static int
get_mode(bool privatemode, int arg)
{
  if (privatemode) { /* DECRQM for DECSET/DECRST: DEC private mode */
    switch (arg) {
      when 1:  /* DECCKM: application cursor keys */
        return 2 - term.app_cursor_keys;
      when 66:  /* DECNKM: application keypad */
        return 2 - term.app_keypad;
      when 2:  /* DECANM: VT100/VT52 mode */
        // Check USASCII for character sets G0-G3
        for (uint i = 0; i < lengthof(term.curs.csets); i++)
          if (term.curs.csets[i] != CSET_ASCII)
            return 2;
        return 1;
      when 3:  /* DECCOLM: 80/132 columns */
        return 2 - term.reset_132;
      when 5:  /* DECSCNM: reverse video */
        return 2 - term.rvideo;
      when 6:  /* DECOM: DEC origin mode */
        return 2 - term.curs.origin;
      when 7:  /* DECAWM: auto wrap */
        return 2 - term.autowrap;
      when 45:  /* xterm: reverse (auto) wraparound */
        return 2 - term.rev_wrap;
      when 8:  /* DECARM: auto key repeat */
        return 2 - term.auto_repeat;
        //return 3; // ignored
      when 9:  /* X10_MOUSE */
        return 2 - (term.mouse_mode == MM_X10);
      when 12: /* AT&T 610 blinking cursor */
        return 2 - term.cursor_blinkmode;
      when 25: /* DECTCEM: enable/disable cursor */
        return 2 - term.cursor_on;
      when 30: /* Show/hide scrollbar */
        return 2 - term.show_scrollbar;
      when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
        return 2 - term.deccolm_allowed;
      when 42: /* DECNRCM: national replacement character sets */
        return 2 - term.decnrc_enabled;
      when 44: /* margin bell (xterm) */
        return 2 - term.margin_bell;
      when 67: /* DECBKM: backarrow key mode */
        return 2 - term.backspace_sends_bs;
      when 69: /* DECLRMM: enable left and right margin mode DECSLRM */
        return 2 - term.lrmargmode;
      when 80: /* DECSDM: SIXEL display mode */
        return 2 - !term.sixel_display;
      when 1000: /* VT200_MOUSE */
        return 2 - (term.mouse_mode == MM_VT200);
      when 1002: /* BTN_EVENT_MOUSE */
        return 2 - (term.mouse_mode == MM_BTN_EVENT);
      when 1003: /* ANY_EVENT_MOUSE */
        return 2 - (term.mouse_mode == MM_ANY_EVENT);
      when 1004: /* FOCUS_EVENT_MOUSE */
        return 2 - term.report_focus;
      when 1005: /* Xterm's UTF8 encoding for mouse positions */
        return 2 - (term.mouse_enc == ME_UTF8);
      when 1006: /* Xterm's CSI-style mouse encoding */
        return 2 - (term.mouse_enc == ME_XTERM_CSI);
      when 1016: /* Xterm's CSI-style mouse encoding with pixel resolution */
        return 2 - (term.mouse_enc == ME_PIXEL_CSI);
      when 1015: /* Urxvt's CSI-style mouse encoding */
        return 2 - (term.mouse_enc == ME_URXVT_CSI);
      when 1037:
        return 2 - term.delete_sends_del;
      when 1042:
        return 2 - term.bell_taskbar;
      when 1043:
        return 2 - term.bell_popup;
      when 47: /* alternate screen */
        return 2 - term.on_alt_screen;
      when 1047:       /* alternate screen */
        return 2 - term.on_alt_screen;
      when 1048:       /* save/restore cursor */
        return 4;
      when 1049:       /* cursor & alternate screen */
        return 2 - term.on_alt_screen;
      when 1061:       /* VT220 keyboard emulation */
        return 2 - term.vt220_keys;
      when 2004:       /* xterm bracketed paste mode */
        return 2 - term.bracketed_paste;

      /* Mintty private modes */
      when 7700:       /* CJK ambigous width reporting */
        return 2 - term.report_ambig_width;
      when 7711:       /* Scroll marker in current line */
        return 2 - !!(term.lines[term.curs.y]->lattr & LATTR_MARKED);
      when 7727:       /* Application escape key mode */
        return 2 - term.app_escape_key;
      when 7728:       /* Escape sends FS (instead of ESC) */
        return 2 - term.escape_sends_fs;
      when 7730:       /* Sixel scrolling end position */
        return 2 - term.sixel_scrolls_left;
      when 7766:       /* 'B': Show/hide scrollbar (if enabled in config) */
        return 2 - term.show_scrollbar;
      when 7767:       /* 'C': Changed font reporting */
        return 2 - term.report_font_changed;
      when 7783:       /* 'S': Shortcut override */
        return 2 - term.shortcut_override;
      when 1007:       /* Alternate Scroll Mode, xterm */
        return 2 - term.wheel_reporting_xterm;
      when 7786:       /* 'V': Mousewheel reporting */
        return 2 - term.wheel_reporting;
      when 7787:       /* 'W': Application mousewheel mode */
        return 2 - term.app_wheel;
      when 7796:       /* Bidi disable in current line */
        return 2 - !!(term.lines[term.curs.y]->lattr & LATTR_NOBIDI);
      when 77096:      /* Bidi disable */
        return 2 - term.disable_bidi;
      when 8452:       /* Sixel scrolling end position right */
        return 2 - term.sixel_scrolls_right;
      when 77000 ... 77031: { /* Application control key modes */
        int ctrl = arg - 77000;
        return 2 - !!(term.app_control & (1 << ctrl));
      }
      when 2500: /* bidi box graphics mirroring */
        return 2 - !!(term.curs.bidimode & LATTR_BOXMIRROR);
      when 2501: /* bidi direction auto-detection */
        return 2 - !(term.curs.bidimode & LATTR_BIDISEL);
      otherwise:
        return 0;
    }
  }
  else { /* DECRQM for SM/RM: mode */
    switch (arg) {
      when 4:  /* IRM: insert mode */
        return 2 - term.insert;
      when 8: /* BDSM: bidirectional support mode */
        return 2 - !(term.curs.bidimode & LATTR_NOBIDI);
      when 12: /* SRM: echo mode */
        return 2 - !term.echoing;
      when 20: /* LNM: Return sends ... */
        return 2 - term.newline_mode;
#ifdef support_Wyse_cursor_modes
      when 33: /* WYSTCURM: steady Wyse cursor */
        return 2 - (!term.cursor_blinkmode);
      when 34: /* WYULCURM: Wyse underline cursor */
        if (term.cursor_type <= 1)
          return 2 - (term.cursor_type == 1);
        else
          return 0;
#endif
      otherwise:
        return 0;
    }
  }
}

struct mode_entry {
  int mode, val;
};
static struct mode_entry * mode_stack = 0;
static int mode_stack_len = 0;

static void
push_mode(int mode, int val)
{
  struct mode_entry * new_stack = renewn(mode_stack, mode_stack_len + 1);
  if (new_stack) {
    mode_stack = new_stack;
    mode_stack[mode_stack_len].mode = mode;
    mode_stack[mode_stack_len].val = val;
    mode_stack_len++;
  }
}

static int
pop_mode(int mode)
{
  for (int i = mode_stack_len - 1; i >= 0; i--)
    if (mode_stack[i].mode == mode) {
      int val = mode_stack[i].val;
      mode_stack_len--;
      for (int j = i; j < mode_stack_len; j++)
        mode_stack[j] = mode_stack[j + 1];
      struct mode_entry * new_stack = renewn(mode_stack, mode_stack_len);
      if (new_stack)
        mode_stack = new_stack;
      return val;
    }
  return -1;
}

struct cattr_entry {
  cattr ca;
  cattrflags mask;
};
static struct cattr_entry cattr_stack[10];
static int cattr_stack_len = 0;

static void
push_attrs(cattr ca, cattrflags caflagsmask)
{
  if (cattr_stack_len == lengthof(cattr_stack)) {
    for (int i = 1; i < cattr_stack_len; i++)
      cattr_stack[i - 1] = cattr_stack[i];
    cattr_stack_len--;
  }
  //printf("push_attrs[%d] %llX\n", cattr_stack_len, caflagsmask);
  cattr_stack[cattr_stack_len].ca = ca;
  cattr_stack[cattr_stack_len].mask = caflagsmask;
  cattr_stack_len++;
}

static bool
pop_attrs(cattr * _ca, cattrflags * _caflagsmask)
{
  if (!cattr_stack_len)
    return false;
  cattr_stack_len--;
  //printf("pop_attrs[%d] %llX\n", cattr_stack_len, cattr_stack[cattr_stack_len].mask);
  *_ca = cattr_stack[cattr_stack_len].ca;
  *_caflagsmask = cattr_stack[cattr_stack_len].mask;
  return true;
}

static COLORREF * colours_stack[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int colours_cur = 0;
static int colours_num = 0;

static void
push_colours(uint ix)
{
  if (ix > 10)
    return;

  if (ix) {  // store
    colours_cur = ix;
    ix--;
  }
  else {  // push
    if (colours_cur < 10) {
      ix = colours_cur;
      colours_cur++;
    }
    else
      return;
  }
  //printf("push %d\n", ix);

  if (!colours_stack[ix]) {
    colours_stack[ix] = malloc(COLOUR_NUM * sizeof(COLORREF));
    if (colours_stack[ix])
      colours_num++;
  }
  if (colours_stack[ix])
    memcpy(colours_stack[ix], colours, COLOUR_NUM * sizeof(COLORREF));
}

static void
pop_colours(uint ix)
{
  if (ix > 10)
    return;

  if (ix) {  // retrieve
    colours_cur = ix;
    ix--;
  }
  else {  // pop
    if (colours_cur) {
      colours_cur--;
      ix = colours_cur;
    }
    else
      return;
  }
  //printf("pop %d\n", ix);

  if (colours_stack[ix])
    memcpy(colours, colours_stack[ix], COLOUR_NUM * sizeof(COLORREF));
}

/*
 * dtterm window operations and xterm extensions.
   CSI Ps ; Ps ; Ps t
 */
static void
do_winop(void)
{
  int arg1 = term.csi_argv[1], arg2 = term.csi_argv[2];
  if (*cfg.suppress_win && contains(cfg.suppress_win, term.csi_argv[0]))
    // skip suppressed window operation
    return;
  switch (term.csi_argv[0]) {
    when 1: win_set_iconic(false);
    when 2: win_set_iconic(true);
    when 3: win_set_pos(arg1, arg2);
    when 4: win_set_pixels(arg1, arg2);
    when 5:
      if (term.csi_argc != 1)
        return;
      win_set_zorder(true);  // top
    when 6:
      if (term.csi_argc != 1)
        return;
      win_set_zorder(false); // bottom
    when 7: win_invalidate_all(false);  // refresh
    when 8: {
      int def1 = term.csi_argv_defined[1], def2 = term.csi_argv_defined[2];
      int rows, cols;
      win_get_screen_chars(&rows, &cols);
      win_set_chars(arg1 ?: def1 ? rows : term.rows, arg2 ?: def2 ? cols : term.cols);
    }
    when 9: {
      if (term.csi_argc != 2)
        return;
      // Ps = 9 ; 0  -> Restore maximized window.
      // Ps = 9 ; 1  -> Maximize window (i.e., resize to screen size).
      // Ps = 9 ; 2  -> Maximize window vertically.
      // Ps = 9 ; 3  -> Maximize window horizontally.
      int rows0 = term.rows0, cols0 = term.cols0;
      if (arg1 == 2) {
        // maximize window vertically
        win_set_geom(0, -1, 0, -1);
        term.rows0 = rows0; term.cols0 = cols0;
      }
      else if (arg1 == 3) {
        // maximize window horizontally
        win_set_geom(-1, 0, -1, 0);
        term.rows0 = rows0; term.cols0 = cols0;
      }
      else if (arg1 == 1) {
        win_maximise(1);
        term.rows0 = rows0; term.cols0 = cols0;
      }
      else if (arg1 == 0) {
        win_maximise(0);
        win_set_chars(rows0, cols0);
      }
      usleep(1000);
    }
    when 10:
      if (term.csi_argc != 2)
        return;
      // Ps = 1 0 ; 0  -> Undo full-screen mode.
      // Ps = 1 0 ; 1  -> Change to full-screen.
      // Ps = 1 0 ; 2  -> Toggle full-screen.
      if (arg1 == 2)
        win_maximise(-2);
      else if (arg1 == 1 || arg1 == 0)
        win_maximise(arg1 ? 2 : 0);
      usleep(1000);
    when 11: child_write(win_is_iconic() ? "\e[2t" : "\e[1t", 4);
    when 13: {
      int x, y;
      win_get_scrpos(&x, &y, arg1 == 2);
      child_printf("\e[3;%u;%ut", (ushort)x, (ushort)y);
    }
    when 14: {
      int height, width;
      win_get_pixels(&height, &width, arg1 == 2);
      child_printf("\e[4;%d;%dt", height, width);
    }
    when 15: {
      int w, h;
      search_monitors(&w, &h, 0, false, 0);
      child_printf("\e[5;%d;%dt", h, w);
    }
    when 16: child_printf("\e[6;%d;%dt", cell_height, cell_width);
    when 18: child_printf("\e[8;%d;%dt", term.rows, term.cols);
    when 19: {
#ifdef size_of_monitor_only
#warning not what xterm reports
      int rows, cols;
      win_get_screen_chars(&rows, &cols);
      child_printf("\e[9;%d;%dt", rows, cols);
#else
      int w, h;
      search_monitors(&w, &h, 0, false, 0);
      child_printf("\e[9;%d;%dt", h / cell_height, w / cell_width);
#endif
    }
    when 22:
      if (arg1 == 0 || arg1 == 2)
        win_save_title();
    when 23:
      if (arg1 == 0 || arg1 == 2)
        win_restore_title();
  }
}

static void
set_taskbar_progress(int state, int percent)
{
  //printf("set_taskbar_progress (%d) %d %d%%\n", term.detect_progress, state, percent);
  if (state == 0 && percent < 0) {  // disable progress indication
    // skipping this if percent < 0 to allow percent-only setting with state 0
    taskbar_progress(-9);
    term.detect_progress = 0;
  }
  else if (state == 8) {  // "busy"
    taskbar_progress(-8);
    term.detect_progress = 0;
  }
  else if (state == 10) {  // reset to default
    term.detect_progress = cfg.progress_bar;
    taskbar_progress(-9);
  }
  else if (state <= 3) {
    if (state > 0)
      taskbar_progress(- state);
    if (percent >= 0) {
      // if we disable (above), then request percentage only (here), 
      // colour will be 1/green regardless of previous/configured setting;
      // to improve this, we'd have to introduce another variable,
      // term.previous_progress
      taskbar_progress(percent);
      term.detect_progress = 0;
    }
    else  // enable automatic progress detection
      term.detect_progress = state;
  }
}

static void
do_csi(uchar c)
{
  term_cursor *curs = &term.curs;
  int arg0 = term.csi_argv[0], arg1 = term.csi_argv[1];
  if (arg0 < 0)
    arg0 = 0;
  if (arg1 < 0)
    arg1 = 0;
  int arg0_def1 = arg0 ?: 1;  // first arg with default 1

  // DECRQM quirk
  if (term.esc_mod == 0xFF && esc_mod0 == '?' && esc_mod1 == '$' && c == 'p')
    term.esc_mod = '$';

  switch (CPAIR(term.esc_mod, c)) {
    when CPAIR('!', 'p'):     /* DECSTR: soft terminal reset */
      term_reset(false);
    when 'b': {      /* REP: repeat preceding character */
      cattr cur_attr = term.curs.attr;
      term.curs.attr = last_attr;
      wchar h = last_high, c = last_char;
      if (last_char)
        for (int i = 0; i < arg0_def1; i++)
          write_ucschar(h, c, last_width);
      term.curs.attr = cur_attr;
    }
    when 'A':        /* CUU: move up N lines */
      move(curs->x, curs->y - arg0_def1, 1);
    when 'e':        /* VPR: move down N lines */
      move(curs->x, curs->y + arg0_def1, 1);
    when 'B':        /* CUD: Cursor down */
      move(curs->x, curs->y + arg0_def1, 1);
    when 'c':        /* Primary DA: report device/terminal type */
      if (!arg0)
        write_primary_da();
    when CPAIR('>', 'c'):     /* Secondary DA: report device version */
      if (!arg0) {
        if (cfg.charwidth % 10)
          child_printf("\e[>77;%u;%uc", DECIMAL_VERSION, UNICODE_VERSION);
        else
          child_printf("\e[>77;%u;0c", DECIMAL_VERSION);
      }
    when CPAIR('>', 'q'):     /* Report terminal name and version */
      if (!arg0)
        child_printf("\eP>|%s %s\e\\", APPNAME, VERSION);
    when 'a':        /* HPR: move right N cols */
      move(curs->x + arg0_def1, curs->y, 1);
    when 'C':        /* CUF: Cursor right */
      move(curs->x + arg0_def1, curs->y, 1);
    when 'D':        /* CUB: move left N cols */
      if (arg0_def1 > curs->x) {
        arg0_def1 -= curs->x + 1;
        move(0, curs->y, 1);
        write_backspace();
        move(curs->x - arg0_def1, curs->y, 1);
      }
      else
        move(curs->x - arg0_def1, curs->y, 1);
      enable_progress();
    when 'E':        /* CNL: move down N lines and CR */
      move(0, curs->y + arg0_def1, 1);
    when 'F':        /* CPL: move up N lines and CR */
      move(0, curs->y - arg0_def1, 1);
    when 'G' or '`': { /* CHA or HPA: set horizontal position */
      short x = (curs->origin ? term.marg_left : 0) + arg0_def1 - 1;
      if (x < curs->x)
        enable_progress();
      move(x, curs->y, curs->origin ? 2 : 0);
    }
    when 'd':        /* VPA: set vertical position */
      move(curs->x,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'H' or 'f':  /* CUP or HVP: set horiz. and vert. positions at once */
      move((curs->origin ? term.marg_left : 0) + (arg1 ?: 1) - 1,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'I':  /* CHT: move right N TABs */
      for (int i = 0; i < arg0_def1; i++)
        write_tab();
    when 'J' or CPAIR('?', 'J'):  /* ED/DECSED: (selective) erase in display */
      if (arg0 == 3) { /* Erase Saved Lines (xterm) */
        // don't care if (term.esc_mod) // ignore selective
        term_clear_scrollback();
        term.disptop = 0;
      }
      else if (arg0 <= 2) {
        bool above = arg0 == 1 || arg0 == 2;
        bool below = arg0 == 0 || arg0 == 2;
        term_erase(term.esc_mod | term.iso_guarded_area, false, above, below);
      }
    when 'K' or CPAIR('?', 'K'):  /* EL/DECSEL: (selective) erase in line */
      if (arg0 <= 2) {
        bool right = arg0 == 0 || arg0 == 2;
        bool left  = arg0 == 1 || arg0 == 2;
        term_erase(term.esc_mod | term.iso_guarded_area, true, left, right);
      }
    when 'X': {      /* ECH: write N spaces w/o moving cursor */
      termline *line = term.lines[curs->y];
      int cols = min(line->cols, line->size);
      int n = min(arg0_def1, cols - curs->x);
      if (n > 0) {
        int p = curs->x;
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + n, curs->y);
        while (n--) {
          if (!term.iso_guarded_area ||
              !(line->chars[p].attr.attr & ATTR_PROTECTED)
             )
            line->chars[p] = term.erase_char;
          p++;
        }
      }
    }
    when 'L':        /* IL: insert lines */
      if (curs->y >= term.marg_top && curs->y <= term.marg_bot
       && curs->x >= term.marg_left && curs->x <= term.marg_right
         )
      {
        term_do_scroll(curs->y, term.marg_bot, -arg0_def1, false);
        curs->x = term.marg_left;
      }
    when 'M':        /* DL: delete lines */
      if (curs->y >= term.marg_top && curs->y <= term.marg_bot
       && curs->x >= term.marg_left && curs->x <= term.marg_right
         )
      {
        term_do_scroll(curs->y, term.marg_bot, arg0_def1, true);
        curs->x = term.marg_left;
      }
    when '@':        /* ICH: insert chars */
      insert_char(arg0_def1);
    when 'P':        /* DCH: delete chars */
      insert_char(-arg0_def1);
    when 'h' or CPAIR('?', 'h'):  /* SM/DECSET: set (private) modes */
      set_modes(true);
    when 'l' or CPAIR('?', 'l'):  /* RM/DECRST: reset (private) modes */
      set_modes(false);
    when CPAIR('?', 's'): { /* Save DEC Private Mode (DECSET) values */
      int arg = term.csi_argv[0];
      int val = get_mode(true, arg);
      if (val)
        push_mode(arg, val);
    }
    when CPAIR('?', 'r'): { /* Restore DEC Private Mode (DECSET) values */
      int arg = term.csi_argv[0];
      int val = pop_mode(arg);
      if (val >= 0) {
        term.csi_argc = 1;
        set_modes(val & 1);
      }
    }
    when CPAIR('#', '{') or CPAIR('#', 'p'): { /* Push video attributes onto stack (XTPUSHSGR) */
      cattr ca = term.curs.attr;
      cattrflags caflagsmask = 0;

      void set_push(int attr) {
        switch (attr) {
          when 1: caflagsmask |= ATTR_BOLD | ATTR_SHADOW;
          when 2: caflagsmask |= ATTR_DIM;
          when 3: caflagsmask |= ATTR_ITALIC;
          when 4 or 21: caflagsmask |= UNDER_MASK;
          when 5 or 6: caflagsmask |= ATTR_BLINK | ATTR_BLINK2;
          when 7: caflagsmask |= ATTR_REVERSE;
          when 8: caflagsmask |= ATTR_INVISIBLE | ATTR_OVERSTRIKE;
          when 9: caflagsmask |= ATTR_STRIKEOUT;
          when 20: caflagsmask |= FONTFAM_MASK;
          when 53: caflagsmask |= ATTR_OVERL;
          when 58: caflagsmask |= ATTR_ULCOLOUR;
          when 30 or 10: caflagsmask |= ATTR_FGMASK;
          when 31 or 11: caflagsmask |= ATTR_BGMASK;
          when 73: caflagsmask |= ATTR_SUPERSCR;
          when 74: caflagsmask |= ATTR_SUBSCR;
        }
      }

      if (!term.csi_argv_defined[0])
        for (int a = 1; a < 90; a++)
          set_push(a);
      else
        for (uint i = 0; i < term.csi_argc; i++) {
          //printf("XTPUSHSGR[%d] %d\n", i, term.csi_argv[i]);
          set_push(term.csi_argv[i]);
        }
      if ((ca.attr & caflagsmask & ATTR_FGMASK) != TRUE_COLOUR)
        ca.truefg = 0;
      if ((ca.attr & caflagsmask & ATTR_BGMASK) != TRUE_COLOUR << ATTR_BGSHIFT)
        ca.truebg = 0;
      if (!(caflagsmask & ATTR_ULCOLOUR))
        ca.ulcolr = (colour)-1;
      // push
      //printf("XTPUSHSGR &%llX %llX %06X %06X %06X\n", caflagsmask, ca.attr, ca.truefg, ca.truebg, ca.ulcolr);
      push_attrs(ca, caflagsmask);
    }
    when CPAIR('#', '}') or CPAIR('#', 'q'): { /* Pop video attributes from stack (XTPOPSGR) */
      //printf("XTPOPSGR\n");
      // pop
      cattr ca;
      cattrflags caflagsmask;
      if (pop_attrs(&ca, &caflagsmask)) {
        //printf("XTPOPSGR &%llX %llX %06X %06X %06X\n", caflagsmask, ca.attr, ca.truefg, ca.truebg, ca.ulcolr);
        // merge
        term.curs.attr.attr = (term.curs.attr.attr & ~caflagsmask)
                              | (ca.attr & caflagsmask);
        if ((ca.attr & caflagsmask & ATTR_FGMASK) == TRUE_COLOUR)
          term.curs.attr.truefg = ca.truefg;
        if ((ca.attr & caflagsmask & ATTR_BGMASK) == TRUE_COLOUR << ATTR_BGSHIFT)
          term.curs.attr.truebg = ca.truebg;
        if (caflagsmask & ATTR_ULCOLOUR)
          term.curs.attr.ulcolr = ca.ulcolr;
      }
    }
    when CPAIR('#', 'P'):  /* Push dynamic colours onto stack (XTPUSHCOLORS) */
      push_colours(arg0);
    when CPAIR('#', 'Q'):  /* Pop dynamic colours from stack (XTPOPCOLORS) */
      pop_colours(arg0);
      win_invalidate_all(false);  // refresh
    when CPAIR('#', 'R'):  /* Report colours stack entry (XTREPORTCOLORS) */
      child_printf("\e[?%d;%d#Q", colours_cur, colours_num);
    when CPAIR('$', 'p'): { /* DECRQM: request (private) mode */
      int arg = term.csi_argv[0];
      child_printf("\e[%s%u;%u$y",
                   esc_mod0 ? "?" : "",
                   arg,
                   get_mode(esc_mod0, arg));
    }
    when 'i' or CPAIR('?', 'i'):  /* MC: Media copy */
      if (arg0 == 5 && *cfg.printer) {
        term.printing = true;
        term.only_printing = !term.esc_mod;
        term.print_state = 0;
        if (*cfg.printer == '*')
          printer_start_job(printer_get_default());
        else
          printer_start_job(cfg.printer);
      }
      else if (arg0 == 4 && term.printing) {
        // Drop escape sequence from print buffer and finish printing.
        while (term.printbuf[--term.printbuf_pos] != '\e');
        term_print_finish();
      }
      else if (arg0 == 10 && !term.esc_mod) {
        term_export_html(false);
      }
#ifdef support_SVG
      else if (arg0 == 11 && !term.esc_mod) {
        term_export_svg();
      }
#endif
      else if (arg0 == 12 && !term.esc_mod) {
        term_save_image();
      }
      else if (arg0 == 0 && !term.esc_mod) {
        print_screen();
      }
    when 'g':        /* TBC: clear tabs */
      if (!arg0)
        term.tabs[curs->x] = false;
      else if (arg0 == 3) {
        for (int i = 0; i < term.cols; i++)
          term.tabs[i] = false;
        term.newtab = 0;  // don't set new default tabs on resize
      }
    when 'r': {      /* DECSTBM: set scrolling region */
      int top = arg0_def1 - 1;
      int bot = (arg1 ? min(arg1, term.rows) : term.rows) - 1;
      if (bot > top) {
        term.marg_top = top;
        term.marg_bot = bot;
        curs->x = curs->origin ? term.marg_left : 0;
        curs->y = curs->origin ? term.marg_top : 0;
      }
    }
    when 's':
      if (term.lrmargmode) {  /* DECSLRM: set left and right margin */
        int left = arg0_def1 - 1;
        int right = (arg1 ? min(arg1, term.cols) : term.cols) - 1;
        if (right > left) {
          term.marg_left = left;
          term.marg_right = right;
          curs->x = curs->origin ? term.marg_left : 0;
          curs->y = curs->origin ? term.marg_top : 0;
        }
      }
      else           /* SCOSC: save cursor */
        save_cursor();
    when 'u':        /* SCORC: restore cursor */
      restore_cursor();
    when 'm':        /* SGR: set graphics rendition */
      do_sgr();
    when 't':
     /*
      * VT340/VT420 sequence DECSLPP, for setting the height of the window.
      * DEC only allowed values 24/25/36/48/72/144, so dtterm and xterm
      * claimed values below 24 for various window operations, 
      * and also allowed any number of rows from 24 and above to be set.
      */
      if (arg0 >= 24) {  /* DECSLPP: set page size - ie window height */
        if (*cfg.suppress_win && contains(cfg.suppress_win, 24))
          ; // skip suppressed window operation
        else {
          win_set_chars(arg0, term.cols);
          term.selected = false;
        }
      }
      else
        do_winop();
    when 'S':        /* SU: Scroll up */
      term_do_scroll(term.marg_top, term.marg_bot, arg0_def1, true);
      curs->wrapnext = false;
    when 'T':        /* SD: Scroll down */
      /* Avoid clash with unsupported hilight mouse tracking mode sequence */
      if (term.csi_argc <= 1) {
        term_do_scroll(term.marg_top, term.marg_bot, -arg0_def1, true);
        curs->wrapnext = false;
      }
    when CPAIR('*', '|'):     /* DECSNLS */
     /*
      * Set number of lines on screen
      * VT420 uses VGA like hardware and can support any size 
      * in reasonable range (24..49 AIUI) with no default specified.
      */
      win_set_chars(arg0 ?: cfg.rows, term.cols);
      term.selected = false;
    when CPAIR('$', '|'):     /* DECSCPP */
     /*
      * Set number of columns per page
      * Docs imply range is only 80 or 132, but I'll allow any.
      */
      win_set_chars(term.rows, arg0 ?: cfg.cols);
      term.selected = false;
    when 'x':        /* DECREQTPARM: report terminal characteristics */
      if (arg0 <= 1)
        child_printf("\e[%u;1;1;120;120;1;0x", arg0 + 2);
    when 'Z': {      /* CBT (Cursor Backward Tabulation) */
      int n = arg0_def1;
      while (--n >= 0 && curs->x > 0) {
        do
          curs->x--;
        while (curs->x > 0 && !term.tabs[curs->x]);
      }
      enable_progress();
    }
    when CPAIR('$', 'w'):     /* DECTABSR: tab stop report */
      if (arg0 == 2) {
        child_printf("\eP2$");
        char sep = 'u';
        for (int i = 0; i < term.cols; i++)
          if (term.tabs[i]) {
            child_printf("%c%d", sep, i + 1);
            sep = '/';
          }
        child_printf("\e\\");
      }
    when CPAIR('>', 'm'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (!arg0)
        term.modify_other_keys = 0;
      else if (arg0 == 4)
        term.modify_other_keys = arg1;
    when CPAIR('>', 'p'):     /* xterm: pointerMode */
      if (arg0 == 0)
        term.hide_mouse = false;
      else if (arg0 == 2)
        term.hide_mouse = true;
    when CPAIR('>', 'n'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (arg0 == 4)
        term.modify_other_keys = 0;
    when CPAIR(' ', 'q'):     /* DECSCUSR: set cursor style */
      term.cursor_type = arg0 ? (arg0 - 1) / 2 : -1;
      term.cursor_blinks = arg0 ? arg0 % 2 : -1;
      if (term.cursor_blinks)
        term.cursor_blink_interval = arg1;
      term.cursor_invalid = true;
      term_schedule_cblink();
    when CPAIR('?', 'c'):  /* Cursor size (Linux console) */
      term.cursor_size = arg0;
    when CPAIR('"', 'q'):  /* DECSCA: select character protection attribute */
      switch (arg0) {
        when 0 or 2:
          term.curs.attr.attr &= ~ATTR_PROTECTED;
          term.iso_guarded_area = false;
        when 1:
          term.curs.attr.attr |= ATTR_PROTECTED;
          term.iso_guarded_area = false;
      }
    when 'n':        /* DSR: device status report */
      if (arg0 == 6)  // CPR
        child_printf("\e[%d;%dR",
                     curs->y + 1 - (curs->origin ? term.marg_top : 0),
                     curs->x + 1 - (curs->origin ? term.marg_left : 0));
      else if (arg0 == 5)
        child_write("\e[0n", 4);  // "in good operating condition"
    when CPAIR('?', 'n'):  /* DSR, DEC specific */
      switch (arg0) {
        when 6:  // DECXCPR
          child_printf("\e[?%d;%dR",  // VT420: third parameter "page"...
                       curs->y + 1 - (curs->origin ? term.marg_top : 0),
                       curs->x + 1 - (curs->origin ? term.marg_left : 0));
        when 15:
          child_printf("\e[?%un", 11 - !!*cfg.printer);
        when 26:  // Keyboard Report
          child_printf("\e[?27;0;%cn", term.has_focus ? '0' : '8');
        // DEC Locator
        when 53 or 55:
          child_printf("\e[?53n");
        when 56:
          child_printf("\e[?57;1n");
      }
    // DEC Locator
    when CPAIR('\'', 'z'): {  /* DECELR: enable locator reporting */
      switch (arg0) {
        when 0:
          if (term.mouse_mode == MM_LOCATOR) {
            term.mouse_mode = 0;
            win_update_mouse();
          }
          term.locator_1_enabled = false;
        when 1:
          term.mouse_mode = MM_LOCATOR;
          win_update_mouse();
        when 2:
          term.locator_1_enabled = true;
          win_update_mouse();
      }
      switch (arg1) {
        when 0 or 2:
          term.locator_by_pixels = false;
        when 1:
          term.locator_by_pixels = true;
      }
      term.locator_rectangle = false;
    }
    when CPAIR('\'', '{'): {  /* DECSLE: select locator events */
      for (uint i = 0; i < term.csi_argc; i++)
        switch (term.csi_argv[i]) {
          when 0: term.locator_report_up = term.locator_report_dn = false;
          when 1: term.locator_report_dn = true;
          when 2: term.locator_report_dn = false;
          when 3: term.locator_report_up = true;
          when 4: term.locator_report_up = false;
        }
    }
    when CPAIR('\'', '|'): {  /* DECRQLP: request locator position */
      if (term.mouse_mode == MM_LOCATOR || term.locator_1_enabled) {
        int x, y, buttons;
        win_get_locator_info(&x, &y, &buttons, term.locator_by_pixels);
        child_printf("\e[1;%d;%d;%d;0&w", buttons, y, x);
        term.locator_1_enabled = false;
      }
      else {
        //child_printf("\e[0&w");  // xterm reports this if loc. compiled in
      }
    }
    when CPAIR('\'', 'w'): {  /* DECEFR: enable filter rectangle */
      int arg2 = term.csi_argv[2], arg3 = term.csi_argv[3];
      int x, y, buttons;
      win_get_locator_info(&x, &y, &buttons, term.locator_by_pixels);
      term.locator_top = arg0 ?: y;
      term.locator_left = arg1 ?: x;
      term.locator_bottom = arg2 ?: y;
      term.locator_right = arg3 ?: x;
      term.locator_rectangle = true;
    }
    when 'q': {  /* DECLL: load keyboard LEDs */
      if (arg0 > 20)
        win_led(arg0 - 20, false);
      else if (arg0)
        win_led(arg0, true);
      else {
        win_led(0, false);
      }
    }
    when CPAIR(' ', 'k'):  /* SCP: ECMA-48 Set Character Path (LTR/RTL) */
      if (arg0 <= 2) {
        if (arg0 == 2)
          curs->bidimode |= LATTR_BIDIRTL;
        else if (arg0 == 1)
          curs->bidimode &= ~LATTR_BIDIRTL;
        else {  // default
          curs->bidimode &= ~(LATTR_BIDISEL | LATTR_BIDIRTL);
        }
        // postpone propagation to line until char is written (put_char)
        //termline *line = term.lines[curs->y];
        //line->lattr &= ~(LATTR_BIDISEL | LATTR_BIDIRTL);
        //line->lattr |= curs->bidimode & ~LATTR_BIDISEL | LATTR_BIDIRTL);
      }
    when CPAIR(' ', 'S'):  /* SPD: ECMA-48 Select Presentation Direction */
      if (arg0 == 0)
          curs->bidimode &= ~LATTR_PRESRTL;
      else if (arg0 == 3)
          curs->bidimode |= LATTR_PRESRTL;
#define urows (uint) term.rows
#define ucols (uint) term.cols
    when CPAIR('$', 'v'):  /* DECCRA: VT420 Copy Rectangular Area */
      copy_rect(arg0_def1, arg1 ?: 1, 
                term.csi_argv[2] ?: urows, term.csi_argv[3] ?: ucols,
                // skip term.csi_argv[4] (source page)
                term.csi_argv[5] ?: urows, term.csi_argv[6] ?: ucols
                // skip term.csi_argv[7] (destination page)
                );
    when CPAIR('$', 'x'):  /* DECFRA: VT420 Fill Rectangular Area */
      fill_rect(arg0 ?: ' ', curs->attr, false,
                arg1 ?: 1, term.csi_argv[2] ?: 1,
                term.csi_argv[3] ?: urows, term.csi_argv[4] ?: ucols);
    when CPAIR('$', 'z'):  /* DECERA: VT420 Erase Rectangular Area */
      fill_rect(' ', term.erase_char.attr, false,
                arg0_def1, arg1 ?: 1,
                term.csi_argv[2] ?: urows, term.csi_argv[3] ?: ucols);
    when CPAIR('$', '{'):  /* DECSERA: VT420 Selective Erase Rectangular Area */
      fill_rect(' ', term.erase_char.attr, true,
                arg0_def1, arg1 ?: 1,
                term.csi_argv[2] ?: urows, term.csi_argv[3] ?: ucols);
    when CPAIR('*', 'x'):  /* DECSACE: VT420 Select Attribute Change Extent */
      switch (arg0) {
        when 2: term.attr_rect = true;
        when 0 or 1: term.attr_rect = false;
      }
    when CPAIR('$', 'r')  /* DECCARA: VT420 Change Attributes in Area */
      or CPAIR('$', 't'): {  /* DECRARA: VT420 Reverse Attributes in Area */
      cattrflags a1 = 0, a2 = 0;
      for (uint i = 4; i < term.csi_argc; i++)
        switch (term.csi_argv[i]) {
          when 0: a2 = ATTR_BOLD | ATTR_UNDER | ATTR_BLINK | ATTR_REVERSE;
          when 1: a1 |= ATTR_BOLD;
          when 4: a1 |= ATTR_UNDER;
          when 5: a1 |= ATTR_BLINK;
          when 7: a1 |= ATTR_REVERSE;
          when 22: a2 |= ATTR_BOLD;
          when 24: a2 |= ATTR_UNDER;
          when 25: a2 |= ATTR_BLINK;
          when 27: a2 |= ATTR_REVERSE;
          //when 2: a1 |= ATTR_DIM;
          //when 3: a1 |= ATTR_ITALIC;
          //when 6: a1 |= ATTR_BLINK2;
          //when 8: a1 |= ATTR_INVISIBLE;
          //when 9: a1 |= ATTR_STRIKEOUT;
        }
      a1 &= ~a2;
      if (c == 'r')
        attr_rect(a1, a2, 0, arg0_def1, arg1 ?: 1,
                  term.csi_argv[2] ?: urows, term.csi_argv[3] ?: ucols);
      else
        attr_rect(0, 0, a1, arg0_def1, arg1 ?: 1,
                  term.csi_argv[2] ?: urows, term.csi_argv[3] ?: ucols);
    }
    when CPAIR('*', 'y'): { /* DECRQCRA: VT420 Request Rectangular Checksum */
      uint s = sum_rect(term.csi_argv[2] ?: 1, term.csi_argv[3] ?: 1,
                        term.csi_argv[4] ?: urows, term.csi_argv[5] ?: ucols);
      child_printf("\eP%u!~%04X\e\\", arg0, -s & 0xFFFF);
    }
    when CPAIR('\'', '}'):  /* DECIC: VT420 Insert Columns */
      if (curs->x >= term.marg_left && curs->x <= term.marg_right
       && curs->y >= term.marg_top && curs->y <= term.marg_bot
         )
        insdel_column(curs->x, false, arg0_def1);
    when CPAIR('\'', '~'):  /* DECDC: VT420 Delete Columns */
      if (curs->x >= term.marg_left && curs->x <= term.marg_right
       && curs->y >= term.marg_top && curs->y <= term.marg_bot
         )
        insdel_column(curs->x, true, arg0_def1);
    when CPAIR(' ', 'A'):     /* SR: ECMA-48 shift columns right */
      if (curs->x >= term.marg_left && curs->x <= term.marg_right
       && curs->y >= term.marg_top && curs->y <= term.marg_bot
         )
        insdel_column(term.marg_left, false, arg0_def1);
    when CPAIR(' ', '@'):     /* SR: ECMA-48 shift columns left */
      if (curs->x >= term.marg_left && curs->x <= term.marg_right
       && curs->y >= term.marg_top && curs->y <= term.marg_bot
         )
        insdel_column(term.marg_left, true, arg0_def1);
    when CPAIR('#', 't'):  /* application scrollbar */
      win_set_scrollview(arg0, arg1, term.csi_argc > 2 ? (int)term.csi_argv[2] : -1);
    when CPAIR('<', 't'):  /* TTIMEST: change IME state (Tera Term) */
      win_set_ime(arg0);
    when CPAIR('<', 's'):  /* TTIMESV: save IME state (Tera Term) */
      push_mode(-1, win_get_ime());
    when CPAIR('<', 'r'):  /* TTIMERS: restore IME state (Tera Term) */
      win_set_ime(pop_mode(-1));
    when CPAIR(' ', 't'):     /* DECSWBV: VT520 warning bell volume */
      if (arg0 <= 8)
        term.bell.vol = arg0;
    when CPAIR(' ', 'u'):     /* DECSMBV: VT520 margin bell volume */
      if (!arg0)
        term.marginbell.vol = 8;
      else if (arg0 <= 8)
        term.marginbell.vol = arg0;
    when CPAIR(' ', 'Z'): /* PEC: ECMA-48 Presentation Expand Or Contract */
      if (!arg0)
        curs->width = 0;
      else if (arg0 == 1)   // expanded
        curs->width = 2;
      else if (arg0 == 2) { // condensed
        if (arg1 == 2)      // single-cell zoomed down
          curs->width = 11;
        else
          curs->width = 1;
      }
      else if (arg0 == 22)  // single-cell zoomed down
        curs->width = 11;
#ifdef support_triple_width
      else if (arg0 == 3)   // triple-cell
        curs->width = 3;
#endif
    when CPAIR('-', 'p'): /* DECARR: VT520 Select Auto Repeat Rate */
      if (arg0 <= 30)
        term.repeat_rate = arg0;
    when CPAIR('%', 'q'):  /* setup progress indicator on taskbar icon */
      set_taskbar_progress(arg0, term.csi_argc > 1 ? arg1 : -1);
    when 'y':  /* DECTST */
      if (arg0 == 4) {
        cattr attr = (cattr)
                     {.attr = ATTR_DEFFG | (TRUE_COLOUR << ATTR_BGSHIFT),
                      .truefg = 0, .truebg = 0, .ulcolr = (colour)-1,
                      .link = -1
                     };
        switch (arg1) {
          when 10: attr.truebg = RGB(0, 0, 255);
          when 11: attr.truebg = RGB(255, 0, 0);
          when 12: attr.truebg = RGB(0, 255, 0);
          when 13: attr.truebg = RGB(255, 255, 255);
          otherwise: return;
        }
        for (int i = 0; i < term.rows; i++) {
          termline *line = term.lines[i];
          for (int j = 0; j < term.cols; j++) {
            line->chars[j] =
              (termchar) {.cc_next = 0, .chr = ' ', attr};
          }
          line->lattr = LATTR_NORM;
        }
        term.disptop = 0;
      }
#ifdef suspend_display_update_via_CSI
    when CPAIR('&', 'q'):  /* suspend display update (ms) */
      term.suspend_update = min(arg0, term.rows * term.cols / 8);
      //printf("susp = %d\n", term.suspend_update);
      if (term.suspend_update == 0) {
        do_update();
        // mysteriously, a delay here makes the output flush 
        // more likely to happen, yet not reliably...
        usleep(1000);
      }
#endif
  }
  last_char = 0;  // cancel preceding char for REP
}

/*
 * Fill image area with sixel placeholder characters and set cursor.
 */
static void
fill_image_space(imglist * img)
{
  cattrflags attr0 = term.curs.attr.attr;
  // refer SIXELCH cells to image for display/discard management
  term.curs.attr.imgi = img->imgi;
#ifdef debug_img_disp
  printf("fill %d:%d %d\n", term.curs.y, term.curs.x, img->imgi);
#endif

  short x0 = term.curs.x;
  if (term.sixel_display) {  // sixel display mode
    short y0 = term.curs.y;
    term.curs.y = 0;
    for (int y = 0; y < img->height && y < term.rows; ++y) {
      term.curs.y = y;
      term.curs.x = 0;
      //printf("SIXELCH @%d imgi %d\n", y, term.curs.attr.imgi);
      for (int x = x0; x < x0 + img->width && x < term.cols; ++x)
        write_char(SIXELCH, 1);
    }
    term.curs.y = y0;
    term.curs.x = x0;
  } else {  // sixel scrolling mode
    for (int i = 0; i < img->height; ++i) {
      term.curs.x = x0;
      //printf("SIXELCH @%d imgi %d\n", term.curs.y, term.curs.attr.imgi);
      for (int x = x0; x < x0 + img->width && x < term.cols; ++x)
        write_char(SIXELCH, 1);
      if (i == img->height - 1) {  // in the last line
        if (!term.sixel_scrolls_right) {
          write_linefeed();
          term.curs.x = term.sixel_scrolls_left ? 0: x0;
        }
      } else {
        write_linefeed();
      }
    }
  }

  term.curs.attr.attr = attr0;
}

static void
do_dcs(void)
{
  // Implemented:
  // DECRQSS (Request Status String)
  // DECAUPSS (Assign User-Preferred Supplemental Set)
  // DECSIXEL
  // No DECUDK (User-Defined Keys) or xterm termcap/terminfo data.

  char *s = term.cmd_buf;
  if (!term.cmd_len)
    *s = 0;
  //printf("DCS %04X state %d <%s>\n", term.dcs_cmd, term.state, s);

  switch (term.dcs_cmd) {

  when CPAIR('!', 'u'):  // DECAUPSS
    if (term.state == DCS_ESCAPE) {
      ushort nrc_code = 0;
      if (term.cmd_len == 1)
        nrc_code = *s;
      else if (term.cmd_len == 2)
        nrc_code = CPAIR(s[0], s[1]);
      term_cset cs = lookup_cset(nrc_code, 7, false);
      if (cs) {
        term.curs.decsupp = cs;
        term_update_cs();
        return;
      }
    }

  when 'q': {
   sixel_state_t * st = (sixel_state_t *)term.imgs.parser_state;
   int status = -1;

   switch (term.state) {
    when DCS_PASSTHROUGH:
      if (!st)
        return;
      status = sixel_parser_parse(st, (unsigned char *)s, term.cmd_len);
      if (status < 0) {
        sixel_parser_deinit(st);
        //printf("free state 1 %p\n", term.imgs.parser_state);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        term.state = DCS_IGNORE;
        return;
      }

    when DCS_ESCAPE:
      if (!st)
        return;
      status = sixel_parser_parse(st, (unsigned char *)s, term.cmd_len);
      if (status < 0) {
        sixel_parser_deinit(st);
        //printf("free state 2 %p\n", term.imgs.parser_state);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }

      unsigned char * pixels = sixel_parser_finalize(st);
      //printf("sixel_parser_finalize %p\n", pixels);
      sixel_parser_deinit(st);
      if (!pixels) {
        //printf("free state 3 %p\n", term.imgs.parser_state);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }

      short left = term.curs.x;
      short top = term.virtuallines + (term.sixel_display ? 0: term.curs.y);
      int width = (st->image.width -1 ) / st->grid_width + 1;
      int height = (st->image.height -1 ) / st->grid_height + 1;
      int pixelwidth = st->image.width;
      int pixelheight = st->image.height;
      //printf("w %d/%d %d h %d/%d %d\n", pixelwidth, st->grid_width, width, pixelheight, st->grid_height, height);

      imglist * img;
      if (!winimg_new(&img, 0, pixels, 0, left, top, width, height, pixelwidth, pixelheight, false, 0, 0, 0, 0, term.curs.attr.attr & (ATTR_BLINK | ATTR_BLINK2))) {
        free(pixels);
        sixel_parser_deinit(st);
        //printf("free state 4 %p\n", term.imgs.parser_state);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }
      img->cwidth = st->max_x;
      img->cheight = st->max_y;

      fill_image_space(img);

      // add image to image list;
      // replace previous for optimisation in some cases
      if (term.imgs.first == NULL) {
        term.imgs.first = term.imgs.last = img;
      } else {
        // try some optimization: replace existing images if overwritten
#ifdef debug_sixel_list
        printf("do_dcs checking imglist\n");
#endif
#ifdef replace_images
#warning do not replace images in the list anymore
        // with new flicker-reduce strategy of rendering overlapped images,
        // new images should always be added to the end of the queue;
        // completely overlayed images should be collected for removal 
        // during the rendering loop (winimgs_paint),
        // or latest when they are scrolled out of the scrollback buffer
        for (imglist * cur = term.imgs.first; cur; cur = cur->next) {
          if (cur->pixelwidth == cur->width * st->grid_width &&
              cur->pixelheight == cur->height * st->grid_height)
          {
            // if same size, replace
            if (img->top == cur->top && img->left == cur->left &&
                img->width == cur->width &&
                img->height == cur->height)
            {
#ifdef debug_sixel_list
              printf("img replace\n");
#endif
              memcpy(cur->pixels, img->pixels, img->pixelwidth * img->pixelheight * 4);
              cur->imgi = img->imgi;
              winimg_destroy(img);
              return;
            }
            // if new image within area of previous image, ...
#ifdef handle_overlay_images
#warning this creates some crash conditions...
            if (img->top >= cur->top && img->left >= cur->left &&
                img->left + img->width <= cur->left + cur->width &&
                img->top + img->height <= cur->top + cur->height)
            {
              // inject new img into old structure;
              // copy img data in stripes, for unknown reason
              for (y = 0; y < img->pixelheight; ++y) {
                memcpy(cur->pixels +
                         ((img->top - cur->top) * st->grid_height + y) * cur->pixelwidth * 4 +
                         (img->left - cur->left) * st->grid_width * 4,
                       img->pixels + y * img->pixelwidth * 4,
                       img->pixelwidth * 4);
              }
              cur->imgi = img->imgi;
              winimg_destroy(img);
              return;
            }
#endif
          }
        }
#endif
        // append image to list
        img->prev = term.imgs.last;
        term.imgs.last->next = img;
        term.imgs.last = img;
      }

    otherwise: {
      /* parser status initialization */
      colour fg = win_get_colour(FG_COLOUR_I);
      colour bg = win_get_colour(BG_COLOUR_I);
      if (!st) {
        st = term.imgs.parser_state = calloc(1, sizeof(sixel_state_t));
        //printf("alloc state %d -> %p\n", (int)sizeof(sixel_state_t), st);
        sixel_parser_set_default_color(st);
      }
      status = sixel_parser_init(st, fg, bg, term.private_color_registers);
      if (status < 0)
        return;
    }
   }
  }

  when CPAIR('$', 'q'):
   switch (term.state) {
    when DCS_ESCAPE: {     // DECRQSS
      cattr attr = term.curs.attr;
      if (!strcmp(s, "m")) { // SGR
        char buf[90], *p = buf;
        p += sprintf(p, "\eP1$r0");

        if (attr.attr & ATTR_BOLD)
          p += sprintf(p, ";1");
        if (attr.attr & ATTR_DIM)
          p += sprintf(p, ";2");
        if (attr.attr & ATTR_SHADOW)
          p += sprintf(p, ";1:2");
        if (attr.attr & ATTR_ITALIC)
          p += sprintf(p, ";3");

        if (attr.attr & ATTR_BROKENUND)
          if (attr.attr & ATTR_DOUBLYUND)
            p += sprintf(p, ";4:5");
          else
            p += sprintf(p, ";4:4");
        else if ((attr.attr & UNDER_MASK) == ATTR_CURLYUND)
          p += sprintf(p, ";4:3");
        else if (attr.attr & ATTR_UNDER)
          p += sprintf(p, ";4");

        if (attr.attr & ATTR_BLINK)
          p += sprintf(p, ";5");
        if (attr.attr & ATTR_BLINK2)
          p += sprintf(p, ";6");
        if (attr.attr & ATTR_REVERSE)
          p += sprintf(p, ";7");
        if (attr.attr & ATTR_INVISIBLE)
          p += sprintf(p, ";8");
        if (attr.attr & ATTR_OVERSTRIKE)
          p += sprintf(p, ";8:7");
        if (attr.attr & ATTR_STRIKEOUT)
          p += sprintf(p, ";9");
        if ((attr.attr & UNDER_MASK) == ATTR_DOUBLYUND)
          p += sprintf(p, ";21");
        if (attr.attr & ATTR_FRAMED)
          p += sprintf(p, ";51;52");
        if (attr.attr & ATTR_OVERL)
          p += sprintf(p, ";53");
        if (attr.attr & ATTR_SUPERSCR)
          p += sprintf(p, ";73");
        if (attr.attr & ATTR_SUBSCR)
          p += sprintf(p, ";74");

        if (term.curs.oem_acs)
          p += sprintf(p, ";%u", 10 + term.curs.oem_acs);
        else {
          uint ff = (attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
          if (ff)
            p += sprintf(p, ";%u", 10 + ff);
        }

        uint fg = (attr.attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
        if (fg != FG_COLOUR_I) {
          if (fg >= TRUE_COLOUR)
            //p += sprintf(p, ";38;2;%u;%u;%u", attr.truefg & 0xFF, 
            //             (attr.truefg >> 8) & 0xFF, (attr.truefg >> 16) & 0xFF);
            p += sprintf(p, ";38:2::%u:%u:%u", attr.truefg & 0xFF, 
                         (attr.truefg >> 8) & 0xFF, (attr.truefg >> 16) & 0xFF);
          else if (fg < 16)
            p += sprintf(p, ";%u", (fg < 8 ? 30 : 90) + (fg & 7));
          else
            //p += sprintf(p, ";38;5;%u", fg);
            p += sprintf(p, ";38:5:%u", fg);
        }

        uint bg = (attr.attr & ATTR_BGMASK) >> ATTR_BGSHIFT;
        if (bg != BG_COLOUR_I) {
          if (bg >= TRUE_COLOUR)
            //p += sprintf(p, ";48;2;%u;%u;%u", attr.truebg & 0xFF, 
            //             (attr.truebg >> 8) & 0xFF, (attr.truebg >> 16) & 0xFF);
            p += sprintf(p, ";48:2::%u:%u:%u", attr.truebg & 0xFF, 
                         (attr.truebg >> 8) & 0xFF, (attr.truebg >> 16) & 0xFF);
          else if (bg < 16)
            p += sprintf(p, ";%u", (bg < 8 ? 40 : 100) + (bg & 7));
          else
            //p += sprintf(p, ";48;5;%u", bg);
            p += sprintf(p, ";48:5:%u", bg);
        }

        if (attr.attr & ATTR_ULCOLOUR) {
          p += sprintf(p, ";58:2::%u:%u:%u", attr.ulcolr & 0xFF, 
                       (attr.ulcolr >> 8) & 0xFF, (attr.ulcolr >> 16) & 0xFF);
        }

        p += sprintf(p, "m\e\\");  // m for SGR, followed by ST

        child_write(buf, p - buf);
      } else if (!strcmp(s, "r")) {  // DECSTBM (scrolling region margins)
        child_printf("\eP1$r%u;%ur\e\\", term.marg_top + 1, term.marg_bot + 1);
      } else if (!strcmp(s, "s")) {  // DECSLRM (left and right margins)
        child_printf("\eP1$r%u;%us\e\\", term.marg_left + 1, term.marg_right + 1);
      } else if (!strcmp(s, "\"p")) {  // DECSCL (conformance level)
        child_printf("\eP1$r%u;%u\"p\e\\", 65, 1);  // report as VT500 S7C1T
      } else if (!strcmp(s, "\"q")) {  // DECSCA (protection attribute)
        child_printf("\eP1$r%u\"q\e\\", (attr.attr & ATTR_PROTECTED) != 0);
      } else if (!strcmp(s, " q")) {  // DECSCUSR (cursor style)
        child_printf("\eP1$r%u q\e\\", 
                     (term.cursor_type >= 0 ? term.cursor_type * 2 : 0) + 1
                     + !(term.cursor_blinks & 1));
      } else if (!strcmp(s, "t") && term.rows >= 24) {  // DECSLPP (lines)
        child_printf("\eP1$r%ut\e\\", term.rows);
      } else if (!strcmp(s, "$|")) {  // DECSCPP (columns)
        child_printf("\eP1$r%u$|\e\\", term.cols);
      } else if (!strcmp(s, "*|")) {  // DECSNLS (lines)
        child_printf("\eP1$r%u*|\e\\", term.rows);
      } else {
        child_printf("\eP0$r%s\e\\", s);
      }
    }
    otherwise:
      return;
   }

  // https://gitlab.com/gnachman/iterm2/-/wikis/synchronized-updates-spec
  // Begin synchronized update (BSU): ESC P = 1 s Parameters ST
  // End synchronized update (ESU): ESC P = 2 s Parameters ST
  when CPAIR('=', 's'): {
    //printf("DCS =[%u]%u;%us term.state %d <%s>\n", term.csi_argc, term.csi_argv[0], term.csi_argv[1], term.state, s);
    int susp = -1;
    if (term.csi_argv[0] == 1) {
      // calculate default and max timeout
      //susp = term.rows * term.cols / (10 + cfg.display_speedup);
      susp = 420;  // limit of user-requested delay
      // limit timeout if requested
      if (term.csi_argc > 1 && term.csi_argv[1])
        susp = min((int)term.csi_argv[1], susp);
      else
        susp = 150;  // constant default
    }
    else if (term.csi_argv[0] == 2)
      susp = 0;
    if (susp < 0)
      return;

    term.suspend_update = susp;
    if (susp == 0) {
      do_update();
      //usleep(1000);  // flush update not needed here...
    }
  }

  }
}

static void
do_colour_osc(bool has_index_arg, uint i, bool reset)
{
  int osc_num = term.cmd_num;  // 4, 5, 10..19, 104, 105, 110..119
  char *s = term.cmd_buf;
  int index;

do_osc_control:
  //printf("OSC %d <%s>\n", osc_num, s);
  if (has_index_arg) {  // OSC 4, 5, 104, 105
    int osc = i;  // 4, 5
    int len = 0;
    sscanf(s, "%u;%n", &index, &len);
    i = index;
    if ((reset ? len != 0 : len == 0) || i >= COLOUR_NUM)
      return;
    s += len;
    if (osc % 100 == 5) {
      if (i == 0)
        i = BOLD_COLOUR_I;
      else if (i == 2)
        i = BLINK_COLOUR_I;
#ifdef other_color_substitutes
      else if (i == 1)
        i = UNDERLINE_COLOUR_I;
      else if (i == 3)
        i = REVERSE_COLOUR_I;
      else if (i == 4)
        i = ITALIC_COLOUR_I;
#endif
      else if (i > 4)
        return;
      else {
        // skip unimplemented setting, continue to process multiple controls
        i = COLOUR_NUM;
      }
    }
    else if (i >= 256)
      return;
  }

  char * cont = strchr(s, ';');
  if (cont)
    *cont = 0;  // enable colour parsing with subsequent multiple values

  colour c;
  if (i >= COLOUR_NUM) {
    // skip this setting
  }
  else if (reset)
    win_set_colour(i, (colour)-1);
  else if (!strcmp(s, "?")) {
    child_printf("\e]%u;", osc_num);
    if (has_index_arg)
      child_printf("%u;", index);
    c = i < COLOUR_NUM ? colours[i] : 0;  // should not be affected by rvideo
    char * osc_fini = term.state == CMD_ESCAPE ? "\e\\" : "\a";
    child_printf("rgb:%04x/%04x/%04x%s",
                 red(c) * 0x101, green(c) * 0x101, blue(c) * 0x101, osc_fini);
  }
  else if (parse_colour(s, &c))
    win_set_colour(i, c);

  if (cont) {  // support multiple osc controls
    s = cont;  // original ';' position
    s++;
    if (osc_num >= 10 && osc_num <= 19) {  // "dynamic colors"
      int new_num;
      int len = 0;
      sscanf(s, "%u;%n", &new_num, &len);
      if (len) {  // OSC 10;blue;12;red
        s += len;
        osc_num = new_num;
      }
      else  // OSC 10;blue;pink: auto-increment dynamic color index
        osc_num++;
      // adjust i (the extended colour palette index)
      // to the new dynamic color number;
      // what a hack! this should have been done in do_cmd
      switch (osc_num) {
        when 10:   i = FG_COLOUR_I;
        when 11:   i = BG_COLOUR_I;
        when 12:   i = CURSOR_COLOUR_I;
        when 17:   i = SEL_COLOUR_I;
        when 19:   i = SEL_TEXT_COLOUR_I;
        when 15:   i = TEK_FG_COLOUR_I;
        when 16:   i = TEK_BG_COLOUR_I;
        when 18:   i = TEK_CURSOR_COLOUR_I;
        otherwise: i = COLOUR_NUM;
      }
    }
    goto do_osc_control;
  }
}

/*
 * OSC 52: \e]52;[cp0-6];?|base64-string\07"
 * Only system clipboard is supported now.
 */
static void
do_clipboard(void)
{
  char *s = term.cmd_buf;
  char *output;
  int len;
  int ret;

  if (!cfg.allow_set_selection) {
    return;
  }

  while (*s != ';' && *s != '\0') {
    s += 1;
  }
  if (*s != ';') {
    return;
  }
  s += 1;
  if (*s == '?') {
    /* Reading from clipboard is unsupported */
    return;
  }
  len = strlen(s);

  output = malloc(len + 1);
  if (output == NULL) {
    return;
  }

  ret = base64_decode_clip(s, len, output, len);
  if (ret > 0) {
    output[ret] = '\0';
    win_copy_text(output);
  }
  free(output);
}

/*
 * Process OSC command sequences.
 */
static void
do_cmd(void)
{
  char *s = term.cmd_buf;
  s[term.cmd_len] = 0;
  //printf("OSC %d <%s> %s\n", term.cmd_num, s, term.state == CMD_ESCAPE ? "ST" : "BEL");
  char * osc_fini = term.state == CMD_ESCAPE ? "\e\\" : "\a";

  if (*cfg.suppress_osc && contains(cfg.suppress_osc, term.cmd_num))
    // skip suppressed OSC command
    return;

  switch (term.cmd_num) {
    when 0 or 2: win_set_title(s);  // ignore icon title
    when 4:   do_colour_osc(true, 4, false);
    when 5:   do_colour_osc(true, 5, false);
    when 6 or 106: {
      int col, on;
      if (sscanf(term.cmd_buf, "%u;%u", &col, &on) == 2) {
        if (col == 0)
          term.enable_bold_colour = on;
        else if (col == 2)
          term.enable_blink_colour = on;
      }
    }
    when 104: do_colour_osc(true, 4, true);
    when 105: do_colour_osc(true, 5, true);
    when 10:  do_colour_osc(false, FG_COLOUR_I, false);
    when 11:  if (strchr("*_%=", *term.cmd_buf)) {
                wchar * bn = cs__mbstowcs(term.cmd_buf);
                wstrset(&cfg.background, bn);
                free(bn);
                if (*term.cmd_buf == '%')
                  scale_to_image_ratio();
                win_invalidate_all(true);
              }
              else
                do_colour_osc(false, BG_COLOUR_I, false);
    when 12:  do_colour_osc(false, CURSOR_COLOUR_I, false);
    when 17:  do_colour_osc(false, SEL_COLOUR_I, false);
    when 19:  do_colour_osc(false, SEL_TEXT_COLOUR_I, false);
    when 15:  do_colour_osc(false, TEK_FG_COLOUR_I, false);
    when 16:  do_colour_osc(false, TEK_BG_COLOUR_I, false);
    when 18:  do_colour_osc(false, TEK_CURSOR_COLOUR_I, false);
    when 110: do_colour_osc(false, FG_COLOUR_I, true);
    when 111: do_colour_osc(false, BG_COLOUR_I, true);
    when 112: do_colour_osc(false, CURSOR_COLOUR_I, true);
    when 117: do_colour_osc(false, SEL_COLOUR_I, true);
    when 119: do_colour_osc(false, SEL_TEXT_COLOUR_I, true);
    when 115: do_colour_osc(false, TEK_FG_COLOUR_I, true);
    when 116: do_colour_osc(false, TEK_BG_COLOUR_I, true);
    when 118: do_colour_osc(false, TEK_CURSOR_COLOUR_I, true);
    when 7:  // Set working directory (from Mac Terminal) for Alt+F2
      // extract dirname from file://host/path scheme
      if (!strncmp(s, "file:", 5))
        s += 5;
      if (!strncmp(s, "//localhost/", 12))
        s += 11;
      else if (!strncmp(s, "///", 3))
        s += 2;
      if (!*s || *s == '/')
        child_set_fork_dir(s);
    when 701:  // Set/get locale (from urxvt).
      if (!strcmp(s, "?"))
        child_printf("\e]701;%s%s", cs_get_locale(), osc_fini);
      else
        cs_set_locale(s);
    when 7721:  // Copy window title to clipboard.
      win_copy_title();
    when 7773: {  // Change icon.
      uint icon_index = 0;
      char *comma = strrchr(s, ',');
      if (comma) {
        char *start = comma + 1, *end;
        icon_index = strtoul(start, &end, 0);
        if (start != end && !*end)
          *comma = 0;
        else
          icon_index = 0;
      }
      win_set_icon(s, icon_index);
    }
    when 7770:  // Change font size.
      if (!strcmp(s, "?"))
        child_printf("\e]7770;%u%s", win_get_font_size(), osc_fini);
      else {
        char *end;
        int i = strtol(s, &end, 10);
        if (*end)
          ; // Ignore if parameter contains unexpected characters
        else if (*s == '+' || *s == '-')
          win_zoom_font(i, false);
        else
          win_set_font_size(i, false);
      }
    when 7777:  // Change font and window size.
      if (!strcmp(s, "?"))
        child_printf("\e]7777;%u%s", win_get_font_size(), osc_fini);
      else {
        char *end;
        int i = strtol(s, &end, 10);
        if (*end)
          ; // Ignore if parameter contains unexpected characters
        else if (*s == '+' || *s == '-')
          win_zoom_font(i, true);
        else
          win_set_font_size(i, true);
      }
    when 7771: {  // Enquire about font support for a list of characters
      if (*s++ != '?')
        return;
      wchar wcs[term.cmd_len];
      uint n = 0;
      while (*s) {
        if (*s++ != ';')
          return;
        wcs[n++] = strtoul(s, &s, 10);
      }
      win_check_glyphs(wcs, n, term.curs.attr.attr);
      s = term.cmd_buf;
      for (size_t i = 0; i < n; i++) {
        *s++ = ';';
        if (wcs[i])
          s += sprintf(s, "%u", wcs[i]);
      }
      *s = 0;
      child_printf("\e]7771;!%s%s", term.cmd_buf, osc_fini);
    }
    when 77119: {  // Indic and Extra characters wide handling
      int what = atoi(s);
      term.wide_indic = false;
      term.wide_extra = false;
      if (what & 1)
        term.wide_indic = true;
      if (what & 2)
        term.wide_extra = true;
    }
    when 52: do_clipboard();
    when 50:
      if (tek_mode) {
        tek_set_font(cs__mbstowcs(s));
        tek_init(false, cfg.tek_glow);
      }
      else {
        uint ff = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
        if (!strcmp(s, "?")) {
          char * fn = cs__wcstombs(win_get_font(ff) ?: W(""));
          child_printf("\e]50;%s%s", fn, osc_fini);
          free(fn);
        }
        else {
          if (ff < lengthof(cfg.fontfams) - 1) {
            wstring wfont = cs__mbstowcs(s);  // let this leak...
            win_change_font(ff, wfont);
          }
        }
      }
    when 22: {  // set mouse pointer style
      wchar * ps = cs__mbstowcs(s);
      set_cursor_style(term.mouse_mode || term.locator_1_enabled, ps);
      free(ps);
    }
    when 7750:
      set_arg_option("Emojis", strdup(s));
      clear_emoji_data();
      win_invalidate_all(false);
    when 8: {  // hyperlink attribute
      char * link = s;
      char * url = strchr(s, ';');
      if (url++ && *url) {
        term.curs.attr.link = putlink(link);
      }
      else
        term.curs.attr.link = -1;
    }
    when 1337: {  // iTerm2 image protocol
                  // https://www.iterm2.com/documentation-images.html
      char * payload = strchr(s, ':');
      if (payload) {
        *payload = 0;
        payload++;
      }

      // verify protocol
      if (0 == strncmp("File=", s, 5))
        s += 5;
      else
        return;

      char * name = 0;
      int width = 0;
      int height = 0;
      int pixelwidth = 0;
      int pixelheight = 0;
      bool pAR = true;
      int crop_x = 0;
      int crop_y = 0;
      int crop_width = 0;
      int crop_height = 0;

      // process parameters
      while (s && *s) {
        char * nxt = strchr(s, ';');
        if (nxt) {
          *nxt = 0;
          nxt++;
        }
        char * sval = strchr(s, '=');
        if (sval) {
          *sval = 0;
          sval++;
        }
        else
          sval = "";
        int val = atoi(sval);
        char * suf = sval;
        while (isdigit((uchar)*suf))
          suf++;
        bool pix = 0 == strcmp("px", suf);
        bool per = 0 == strcmp("%", suf);
        //printf("<%s>=<%s>%d<%s>\n", s, sval, val, suf);

        if (0 == strcmp("name", s))
          name = s;  // can serve as cache id
        else if (0 == strcmp("width", s)) {
          if (pix) {
            pixelwidth = val;
            width = (val - 1) / cell_width + 1;
          }
          else if (per) {
            width = term.cols * val / 100;
            pixelwidth = width * cell_width;
          }
          else {
            width = val;
            pixelwidth = val * cell_width;
          }
        }
        else if (0 == strcmp("height", s)) {
          if (pix) {
            pixelheight = val;
            height = (val - 1) / cell_height + 1;
          }
          else if (per) {
            height = term.rows * val / 100;
            pixelheight = height * cell_height;
          }
          else {
            height = val;
            pixelheight = val * cell_height;
          }
        }
        else if (0 == strcmp("preserveAspectRatio", s)) {
          pAR = val;
        }
        else if (0 == strcmp("cropX", s) || 0 == strcmp("cropLeft", s)) {
          if (pix) {
            crop_x = val;
          }
        }
        else if (0 == strcmp("cropY", s) || 0 == strcmp("cropTop", s)) {
          if (pix) {
            crop_y = val;
          }
        }
        else if (0 == strcmp("cropWidth", s)) {
          if (pix) {
            crop_width = val;
          }
        }
        else if (0 == strcmp("cropHeight", s)) {
          if (pix) {
            crop_height = val;
          }
        }
        else if (0 == strcmp("cropRight", s)) {
          if (pix) {
            crop_width = - val;
          }
        }
        else if (0 == strcmp("cropBottom", s)) {
          if (pix) {
            crop_height = - val;
          }
        }

        s = nxt;
      }

      if (payload) {
#ifdef strip_newlines
#warning not applicable as preprocessing OSC would not pass it here
        char * from = strpbrk(payload, "\r\n");
        if (from) {  // strip new lines
          char * to = from;
          while (*from) {
            if (*from >= ' ')
              *to++ = *from;
            from++;
          }
          *to = 0;
        }
#endif
        int len = strlen(payload);
        int datalen = len - (len / 4);
        void * data = malloc(datalen);
        if (!data)
          return;
        datalen = base64_decode_clip(payload, len, data, datalen);
        if (datalen > 0) {
          // OK
          imglist * img;
          short left = term.curs.x;
          short top = term.virtuallines + term.curs.y;
          if (winimg_new(&img, name, data, datalen, left, top, width, height, pixelwidth, pixelheight, pAR, crop_x, crop_y, crop_width, crop_height, term.curs.attr.attr & (ATTR_BLINK | ATTR_BLINK2))) {
            fill_image_space(img);

            if (term.imgs.first == NULL) {
              term.imgs.first = term.imgs.last = img;
            } else {
              // append image to list
              img->prev = term.imgs.last;
              term.imgs.last->next = img;
              term.imgs.last = img;
            }
          }
          else
            free(data);
        }
        else
          free(data);
      }
    }
    when 9: {
typedef struct {
  char * p;
  int v;
} paramap;
      int scanenum(char * s, int * _i, paramap * p, bool donum) {
        char * sep = strchr(s, ';');
        int len = sep ? (uint)(sep - s) : strlen(s);
        while (p->p) {
          if (0 == strncasecmp(s, p->p, len)) {
            *_i = p->v;
            return len;
          }
          p++;
        }
        if (donum) {
          // fallback scan for number
          int numlen = sscanf(s, "%d", _i);
          if (numlen && numlen == len)
            return numlen;
        }
        // not found
        return 0;
      }

      int cmd;
      int len = scanenum(s, &cmd,
                         (paramap[]){{"4", 4}, {"progress", 4}, {0, 0}},
                         false);
      if (!len || cmd != 4)
        return;
      s += len;

      if (!*s)
        return;
      s++;
      int state;
      len = scanenum(s, &state,
                     (paramap[]){
                                 {"off", 0},
                                 {"default", 10},
                                 {"", 10},
                                 {"green", 1},
                                 {"yellow", 2},
                                 {"red", 3},
                                 {"busy", 8},
                                 {"0", 0},
                                 {"1", 1},
                                 {"4", 2},
                                 {"2", 3},
                                 {"3", 8},
                                 {0, 0}},
                     false);
      if (!len)
        return;
      s += len;

      int percent = -1;
      if (*s) {
        s++;
        sscanf(s, "%d", &percent);
      }

      set_taskbar_progress(state, percent);
    }
  }
}

void
term_print_finish(void)
{
  if (term.printing) {
    printer_write(term.printbuf, term.printbuf_pos);
    free(term.printbuf);
    term.printbuf = 0;
    term.printbuf_size = term.printbuf_pos = 0;
    printer_finish_job();
    term.printing = term.only_printing = false;
  }
}

static void
term_do_write(const char *buf, uint len)
{
  //check e.g. if progress indication is following by CR
  //printf("[%ld] write %02X...%02X\n", mtime(), *buf, buf[len - 1]);

  // Reset cursor blinking.
  term.cblinker = 1;
  term_schedule_cblink();

  short oldy = term.curs.y;

  uint pos = 0;
  while (pos < len) {
    uchar c = buf[pos++];

   /*
    * If we're printing, add the character to the printer buffer.
    */
    if (term.printing) {
      if (term.printbuf_pos >= term.printbuf_size) {
        term.printbuf_size = term.printbuf_size * 4 + 4096;
        term.printbuf = renewn(term.printbuf, term.printbuf_size);
      }
      term.printbuf[term.printbuf_pos++] = c;

     /*
      * If we're in print-only mode, we use a much simpler state machine 
      * designed only to recognise the ESC[4i termination sequence.
      */
      if (term.only_printing) {
        if (c == '\e')
          term.print_state = 1;
        else if (c == '[' && term.print_state == 1)
          term.print_state = 2;
        else if (c == '4' && term.print_state == 2)
          term.print_state = 3;
        else if (c == 'i' && term.print_state == 3) {
          term.printbuf_pos -= 4;
          term_print_finish();
        }
        else
          term.print_state = 0;
        continue;
      }
    }

    switch (term.state) {
      when NORMAL: {
        wchar wc;

        if (term.curs.oem_acs && !memchr("\e\n\r\b", c, 4)) {
          if (term.curs.oem_acs == 2)
            c |= 0x80;
          write_ucschar(0, cs_btowc_glyph(c), 1);
          continue;
        }

        // handle NRC single shift and NRC GR invocation;
        // maybe we should handle control characters first?
        short cset = term.curs.csets[term.curs.gl];
        if (term.curs.cset_single != CSET_ASCII && c > 0x20 && c < 0xFF) {
          cset = term.curs.cset_single;
          term.curs.cset_single = CSET_ASCII;
        }
        else if (term.curs.gr
              //&& (term.decnrc_enabled || !term.decnrc_enabled)
              && term.curs.csets[term.curs.gr] != CSET_ASCII
              && !term.curs.oem_acs && !term.curs.utf
              && c >= 0x80 && c < 0xFF
                )
        {
          // tune C1 behaviour to mimic xterm
          if (c < 0xA0)
            continue;

          c &= 0x7F;
          cset = term.curs.csets[term.curs.gr];
        }

        if (term.vt52_mode) {
          if (term.vt52_mode > 1)
            cset = CSET_VT52DRW;
          else
            cset = CSET_ASCII;
        }
        else if (cset == CSET_DECSUPP)
          cset = term.curs.decsupp;

        switch (cs_mb1towc(&wc, c)) {
          when 0: // NUL or low surrogate
            if (wc)
              pos--;
          when -1: // Encoding error
            if (!tek_mode)
              write_error();
            if (term.in_mb_char || term.high_surrogate)
              pos--;
            term.high_surrogate = 0;
            term.in_mb_char = false;
            cs_mb1towc(0, 0); // Clear decoder state
            continue;
          when -2: // Incomplete character
            term.in_mb_char = true;
            continue;
        }

        term.in_mb_char = false;

        // Fetch previous high surrogate
        wchar hwc = term.high_surrogate;
        term.high_surrogate = 0;

        if (is_low_surrogate(wc)) {
          if (hwc) {
#if HAS_LOCALES
            int width = (cfg.charwidth % 10)
                        ? xcwidth(combine_surrogates(hwc, wc)) :
# ifdef __midipix__
                        wcwidth(combine_surrogates(hwc, wc));
# else
                        wcswidth((wchar[]){hwc, wc}, 2);
# endif
#else
            int width = xcwidth(combine_surrogates(hwc, wc));
#endif
#ifdef support_triple_width
            // do not handle triple-width here
            //if (term.curs.width)
            //  width = term.curs.width % 10;
#endif
            write_ucschar(hwc, wc, width);
          }
          else
            write_error();
          continue;
        }

        if (hwc) // Previous high surrogate not followed by low one
          write_error();

        // ASCII shortcut for some speedup (~5%), earliest applied here
        if (wc >= ' ' && wc <= 0x7E && cset == CSET_ASCII) {
          write_ucschar(0, wc, 1);
          continue;
        }

        if (is_high_surrogate(wc)) {
          term.high_surrogate = wc;
          continue;
        }

        // Non-characters
        if (wc == 0xFFFE || wc == 0xFFFF) {
          write_error();
          continue;
        }

        // Everything else
        wchar NRC(wchar * map)
        {
          static char * rpl = "#@[\\]^_`{|}~";
          char * match = strchr(rpl, c);
          if (match)
            return map[match - rpl];
          else
            return wc;
        }

        cattrflags asav = term.curs.attr.attr;

        switch (cset) {
          when CSET_VT52DRW:  // VT52 "graphics" mode
            if (0x5E <= wc && wc <= 0x7E) {
              uchar dispcode = 0;
              uchar gcode = 0;
              if ('l' <= wc && wc <= 's') {
                dispcode = wc - 'l' + 1;
                gcode = 13;
              }
              else if ('c' <= wc && wc <= 'e') {
                dispcode = 0xF;
              }
              wc = W("^ ￿▮⅟³⁵⁷°±→…÷↓⎺⎺⎻⎻⎼⎼⎽⎽₀₁₂₃₄₅₆₇₈₉¶") [c - 0x5E];
              term.curs.attr.attr |= ((cattrflags)dispcode) << ATTR_GRAPH_SHIFT;
              if (gcode) {
                // extend graph encoding with unused font number
                term.curs.attr.attr &= ~FONTFAM_MASK;
                term.curs.attr.attr |= (cattrflags)gcode << ATTR_FONTFAM_SHIFT;
              }
            }
          when CSET_LINEDRW:  // VT100 line drawing characters
            if (0x60 <= wc && wc <= 0x7E) {
              wchar dispwc = win_linedraw_char(wc - 0x60);
#define draw_vt100_line_drawing_chars
#ifdef draw_vt100_line_drawing_chars
              if ('j' <= wc && wc <= 'x') {
                static uchar linedraw_code[31] = {
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
#if __GNUC__ >= 5
                  0b1001, 0b1100, 0b0110, 0b0011, 0b1111,  // ┘┐┌└┼
                  0x10, 0x20, 0b1010, 0x40, 0x50,          // ⎺⎻─⎼⎽
                  0b0111, 0b1101, 0b1011, 0b1110, 0b0101,  // ├┤┴┬│
#else // < 4.3
                  0x09, 0x0C, 0x06, 0x03, 0x0F,  // ┘┐┌└┼
                  0x10, 0x20, 0x0A, 0x40, 0x50,  // ⎺⎻─⎼⎽
                  0x07, 0x0D, 0x0B, 0x0E, 0x05,  // ├┤┴┬│
#endif
                  0, 0, 0, 0, 0, 0
                };
                uchar dispcode = linedraw_code[wc - 0x60];
                if (dispcode) {
                  uchar gcode = 11;
                  if (dispcode >> 4) {
                    dispcode >>= 4;
                    gcode++;
                  }
                  term.curs.attr.attr |= ((cattrflags)dispcode) << ATTR_GRAPH_SHIFT;
                  // extend graph encoding with unused font numbers
                  term.curs.attr.attr &= ~FONTFAM_MASK;
                  term.curs.attr.attr |= (cattrflags)gcode << ATTR_FONTFAM_SHIFT;
                }
              }
#endif
              wc = dispwc;
            }
          when CSET_TECH:  // DEC Technical character set
            if (c > ' ' && c < 0x7F) {
              // = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎛⎝⎞⎠⎨⎬￿￿╲╱￿￿￿￿￿￿￿≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ￿Σ￿￿√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ￿ν∂πψρστ￿ƒωξυζ←↑→↓")
              // = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎛⎝⎞⎠⎨⎬╶╶╲╱╴╴╳￿￿￿￿≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ￿Σ￿￿√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ￿ν∂πψρστ￿ƒωξυζ←↑→↓")
              // = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎧⎩⎫⎭⎨⎬╶╶╲╱╴╴╳￿￿￿￿≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ￿Σ￿￿√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ￿ν∂πψρστ￿ƒωξυζ←↑→↓")
              wc = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎧⎩⎫⎭⎨⎬╶╶╲╱╴╴╳￿￿￿￿≤≠≥∫∴∝∞÷  ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ￿Σ￿￿√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ￿ν∂πψρστ￿ƒωξυζ←↑→↓")
                   [c - ' ' - 1];
              uchar dispcode = 0;
              if (c <= 0x37) {
                static uchar techdraw_code[23] = {
                  0xE,                          // square root base
                  0, 0, 0, 0, 0,
                  0x8, 0x9, 0xA, 0xB,           // square bracket corners
                  0, 0, 0, 0,                   // curly bracket hooks
                  0, 0,                         // curly bracket middle pieces
                  0x1, 0x2, 0, 0, 0x5, 0x6, 0x7 // sum segments
                };
                dispcode = techdraw_code[c - 0x21];
              }
              else if (c == 0x44)
                dispcode = 0xC;
              else if (c == 0x45)
                dispcode = 0xD;
              term.curs.attr.attr |= ((cattrflags)dispcode) << ATTR_GRAPH_SHIFT;
            }
          when CSET_NL:
            wc = NRC(W("£¾ĳ½|^_`¨ƒ¼´"));  // Dutch
          when CSET_FI:
            wc = NRC(W("#@ÄÖÅÜ_éäöåü"));  // Finnish
          when CSET_FR:
            wc = NRC(W("£à°ç§^_`éùè¨"));  // French
          when CSET_CA:
            wc = NRC(W("#àâçêî_ôéùèû"));  // French Canadian
          when CSET_DE:
            wc = NRC(W("#§ÄÖÜ^_`äöüß"));  // German
          when CSET_IT:
            wc = NRC(W("£§°çé^_ùàòèì"));  // Italian
          when CSET_NO:
            wc = NRC(W("#ÄÆØÅÜ_äæøåü"));  // Norwegian/Danish
          when CSET_PT:
            wc = NRC(W("#@ÃÇÕ^_`ãçõ~"));  // Portuguese
          when CSET_ES:
            wc = NRC(W("£§¡Ñ¿^_`°ñç~"));  // Spanish
          when CSET_SE:
            wc = NRC(W("#ÉÄÖÅÜ_éäöåü"));  // Swedish
          when CSET_CH:
            wc = NRC(W("ùàéçêîèôäöüû"));  // Swiss
          when CSET_DECSPGR   // DEC Supplemental Graphic
            or CSET_DECSUPP:  // DEC Supplemental (user-preferred in VT*)
            if (c > ' ' && c < 0x7F) {
              wc = W("¡¢£￿¥￿§¤©ª«￿￿￿￿°±²³￿µ¶·￿¹º»¼½￿¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ￿ÑÒÓÔÕÖŒØÙÚÛÜŸ￿ßàáâãäåæçèéêëìíîï￿ñòóôõöœøùúûüÿ￿")
                   [c - ' ' - 1];
            }
          // 96-character sets (UK / xterm 336)
          when CSET_GBCHR:  // NRC United Kingdom
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ")
                   [c - ' '];
            }
          when CSET_ISO_Latin_Cyrillic:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ЁЂЃЄЅІЇЈЉЊЋЌ­ЎЏАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя№ёђѓєѕіїјљњћќ§ўџ")
                   [c - ' '];
            }
          when CSET_ISO_Greek_Supp:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ‘’£€₯¦§¨©ͺ«¬­￿―°±²³΄΅Ά·ΈΉΊ»Ό½ΎΏΐΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡ￿ΣΤΥΦΧΨΩΪΫάέήίΰαβγδεζηθικλμνξοπρςστυφχψωϊϋόύώ")
                   [c - ' '];
            }
          when CSET_ISO_Hebrew:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ￿¢£¤¥¦§¨©×«¬­®¯°±²³´µ¶·¸¹÷»¼½¾￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿‗אבגדהוזחטיךכלםמןנסעףפץצקרשת￿￿‎‏")
                   [c - ' '];
            }
          when CSET_ISO_Latin_5:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏĞÑÒÓÔÕÖ×ØÙÚÛÜİŞßàáâãäåæçèéêëìíîïğñòóôõö÷øùúûüışÿ")
                   [c - ' '];
            }
          when CSET_DEC_Greek_Supp:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ¡¢£￿¥￿§¤©ª«￿￿￿￿°±²³￿µ¶·￿¹º»¼½￿¿ϊΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟ￿ΠΡΣΤΥΦΧΨΩάέήί￿όϋαβγδεζηθικλμνξο￿πρστυφχψωςύώ΄￿")
                   [c - ' '];
            }
          when CSET_DEC_Hebrew_Supp:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ¡¢£￿¥￿§¨©×«￿￿￿￿°±²³￿µ¶·￿¹÷»¼½￿¿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿אבגדהוזחטיךכלםמןנסעףפץצקרשת￿￿￿￿")
                   [c - ' '];
            }
          when CSET_DEC_Turkish_Supp:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ¡¢£￿¥￿§¨©ª«￿￿İ￿°±²³￿µ¶·￿¹º»¼½ı¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏĞÑÒÓÔÕÖŒØÙÚÛÜŸŞßàáâãäåæçèéêëìíîïğñòóôõöœøùúûüÿş")
                   [c - ' '];
            }
          when CSET_DEC_Cyrillic:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" ￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿￿юабцдефгхийклмнопярстужвьызшэщчъЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ")
                   [c - ' '];
            }
          when CSET_NRCS_Greek:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`ΑΒΓΔΕΖΗΘΙΚΛΜΝΧΟΠΡΣΤΥΦΞΨΩ￿￿{|}~")
                   [c - ' '];
            }
          when CSET_NRCS_Hebrew:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_אבגדהוזחטיךכלםמןנסעףפץצקרשת{|}~")
                   [c - ' '];
            }
          when CSET_NRCS_Turkish:
            if (c >= ' ' && c <= 0x7F) {
              wc = W(" !\"#$%ğ'()*+,-./0123456789:;<=>?İABCDEFGHIJKLMNOPQRSTUVWXYZŞÖÇÜ_Ğabcdefghijklmnopqrstuvwxyzşöçü")
                   [c - ' '];
            }
          otherwise: ;
        }

        // Some more special graphic renderings
        if (wc >= 0x2580 && wc <= 0x259F) {
          // Block Elements (U+2580-U+259F)
          // ▀▁▂▃▄▅▆▇█▉▊▋▌▍▎▏▐░▒▓▔▕▖▗▘▙▚▛▜▝▞▟
          term.curs.attr.attr |= ((cattrflags)(wc & 0xF)) << ATTR_GRAPH_SHIFT;
          uchar gcode = 14 + ((wc >> 4) & 1);
          // extend graph encoding with unused font numbers
          term.curs.attr.attr &= ~FONTFAM_MASK;
          term.curs.attr.attr |= (cattrflags)gcode << ATTR_FONTFAM_SHIFT;
        }
#ifdef draw_powerline_geometric_symbols
#warning graphical results of this approach are unpleasant; not enabled
        else if (wc >= 0xE0B0 && wc <= 0xE0BF && wc != 0xE0B5 && wc != 0xE0B7) {
          // draw geometric full-cell Powerline symbols,
          // to avoid artefacts at their borders (#943)
          term.curs.attr.attr &= ~FONTFAM_MASK;
          term.curs.attr.attr |= (cattrflags)13 << ATTR_FONTFAM_SHIFT;
          term.curs.attr.attr |= (cattrflags)15 << ATTR_GRAPH_SHIFT;
        }
#endif

        // Determine width of character to be rendered
        int width;
        if (term.wide_indic && wc >= 0x0900 && indicwide(wc))
          width = 2;
        else if (term.wide_extra && wc >= 0x2000 && extrawide(wc)) {
          width = 2;
          // Note: this check is currently not implemented for
          // non-BMP characters (see case if is_low_surrogate(wc) above)
          if (win_char_width(wc, term.curs.attr.attr) < 2)
            term.curs.attr.attr |= TATTR_EXPAND;
        }
        else {
#if HAS_LOCALES
          if (cfg.charwidth % 10)
            width = xcwidth(wc);
          else
            width = wcwidth(wc);
#ifdef support_triple_width
          // do not handle triple-width here
          //if (term.curs.width)
          //  width = term.curs.width % 10;
#endif
# ifdef hide_isolate_marks
          // force bidi isolate marks to be zero-width;
          // however, this is inconsistent with locale width
          if (wc >= 0x2066 && wc <= 0x2069)
            width = 0;  // bidi isolate marks
# endif
#else
          width = xcwidth(wc);
#endif
        }
        if (width < 0 && cfg.printable_controls) {
          if (wc >= 0x80 && wc < 0xA0)
            width = 1;
          else if (wc < ' ' && cfg.printable_controls > 1)
            width = 1;
        }

        // Auto-expanded glyphs
        if (width == 2
            // && wcschr(W("〈〉《》「」『』【】〒〓〔〕〖〗〘〙〚〛"), wc)
            && wc >= 0x3008 && wc <= 0x301B && (wc | 1) != 0x3013
            && win_char_width(wc, term.curs.attr.attr) < 2
            // ensure symmetric handling of matching brackets
            && win_char_width(wc ^ 1, term.curs.attr.attr) < 2)
        {
          term.curs.attr.attr |= TATTR_EXPAND;
        }

        // Control characters
        if (wc < 0x20 || wc == 0x7F) {
          if (!do_ctrl(wc) && c == wc) {
            wc = cs_btowc_glyph(c);
            if (wc != c)
              write_ucschar(0, wc, 1);
            else if (cfg.printable_controls > 1)
              goto goon;
          }
          term.curs.attr.attr = asav;
          continue;

          goon:;
        }

        // Finally, write it and restore cursor attribute
        write_ucschar(0, wc, width);
        term.curs.attr.attr = asav;
      } // end term_write switch (term.state) when NORMAL

      when VT52_Y:
        term.cmd_len = 0;
        term_push_cmd(c);
        term.state = VT52_X;

      when VT52_X:
        term_push_cmd(c);
        do_vt52_move();

      when VT52_FG:
        do_vt52_colour(true, c);

      when VT52_BG:
        do_vt52_colour(false, c);

      when TEK_ESCAPE:
        tek_esc(c);

      when TEK_ADDRESS0 or TEK_ADDRESS:
        if (c == '\a' && tek_mode == TEKMODE_GRAPH0 && term.state == TEK_ADDRESS0) {
          tek_mode= TEKMODE_GRAPH;
        }
        else if (c < ' ')
          tek_ctrl(c);
        else if (tek_mode == TEKMODE_SPECIAL_PLOT && term.state == TEK_ADDRESS0) {
          term.state = TEK_ADDRESS;
          term.cmd_len = 0;
          tek_intensity(c & 0x40, c & 0x37);
        }
        //else if (term.cmd_len > 5) {
        // no length checking here, interferes with previous OSC!
        // let term_push_cmd do it
        //}
        //else if (!(c & 0x60)) {
        // no error checking here, let tek_address catch it
        //}
        else {
          if (term.state == TEK_ADDRESS0) {
            term.state = TEK_ADDRESS;
            term.cmd_len = 0;
          }

          term_push_cmd(c);
          if ((c & 0x60) == 0x40) {
            tek_address(term.cmd_buf);
            term.state = TEK_ADDRESS0;
            if (tek_mode == TEKMODE_GRAPH0)
              tek_mode = TEKMODE_GRAPH;
          }
        }

      when TEK_INCREMENTAL:
        if (c < ' ')
          tek_ctrl(c);
        else if (c == ' ' || c == 'P')
          tek_pen(c == 'P');
        else if (strchr("DEAIHJBF", c))
          tek_step(c);

      when ESCAPE or CMD_ESCAPE:
        if (term.vt52_mode)
          do_vt52(c);
        else if (c < 0x20)
          do_ctrl(c);
        else if (c < 0x30) {
          //term.esc_mod = term.esc_mod ? 0xFF : c;
          if (term.esc_mod) {
            esc_mod0 = term.esc_mod;
            esc_mod1 = c;
            term.esc_mod = 0xFF;
          }
          else {
            esc_mod0 = 0;
            esc_mod1 = 0;
            term.esc_mod = c;
          }
        }
        else if (c == '\\' && term.state == CMD_ESCAPE) {
          /* Process DCS or OSC sequence if we see ST. */
          do_cmd();
          term.state = NORMAL;
        }
        else {
          do_esc(c);
          // term.state: NORMAL/CSI_ARGS/OSC_START/DCS_START/IGNORE_STRING
        }

      when CSI_ARGS:
        if (c < 0x20)
          do_ctrl(c);
        else if (c == ';') {
          if (term.csi_argc < lengthof(term.csi_argv))
            term.csi_argc++;
        }
        else if (c == ':') {
          // support colon-separated sub parameters as specified in
          // ISO/IEC 8613-6 (ITU Recommendation T.416)
          uint i = term.csi_argc - 1;
          term.csi_argv[i] |= SUB_PARS;
          if (term.csi_argc < lengthof(term.csi_argv))
            term.csi_argc++;
        }
        else if (c >= '0' && c <= '9') {
          uint i = term.csi_argc - 1;
          if (i < lengthof(term.csi_argv)) {
            term.csi_argv[i] = 10 * term.csi_argv[i] + c - '0';
            if ((int)term.csi_argv[i] < 0)
              term.csi_argv[i] = INT_MAX;  // capture overflow
            term.csi_argv_defined[i] = 1;
          }
        }
        else if (c < 0x40) {
          //term.esc_mod = term.esc_mod ? 0xFF : c;
          if (term.esc_mod) {
            esc_mod0 = term.esc_mod;
            esc_mod1 = c;
            term.esc_mod = 0xFF;
          }
          else {
            esc_mod0 = 0;
            esc_mod1 = 0;
            term.esc_mod = c;
          }
        }
        else {
          do_csi(c);
          term.state = NORMAL;
        }

      when OSC_START:
        term.cmd_len = 0;
        switch (c) {
          when 'P':  /* Linux palette sequence */
            term.state = OSC_PALETTE;
          when 'R':  /* Linux palette reset */
            win_reset_colours();
            term.state = NORMAL;
          when 'I':  /* OSC set icon file (dtterm, shelltool) */
            term.cmd_num = 7773;
            term.state = OSC_NUM;
          when 'L':  /* OSC set icon label (dtterm, shelltool) */
            term.cmd_num = 1;
            term.state = OSC_NUM;
          when 'l':  /* OSC set window title (dtterm, shelltool) */
            term.cmd_num = 2;
            term.state = OSC_NUM;
          when '0' ... '9':  /* OSC command number */
            term.cmd_num = c - '0';
            term.state = OSC_NUM;
          when ';':
            term.cmd_num = 0;
            term.state = CMD_STRING;
          when '\a':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
          when '\n' or '\r':
            term.state = IGNORE_STRING;
          otherwise:
            term.state = IGNORE_STRING;
        }

      when OSC_NUM:
        switch (c) {
          when '0' ... '9':  /* OSC command number */
            term.cmd_num = term.cmd_num * 10 + c - '0';
            if (term.cmd_num < 0)
              term.cmd_num = -99;  // prevent wrong valid param
          when ';':
            term.state = CMD_STRING;
          when '\a':
            do_cmd();
            term.state = NORMAL;
          when '\e':
            term.state = CMD_ESCAPE;
          when '\n' or '\r':
            term.state = IGNORE_STRING;
          otherwise:
            term.state = IGNORE_STRING;
        }

      when OSC_PALETTE:
        if (isxdigit(c)) {
          // The dodgy Linux palette sequence: keep going until we have
          // seven hexadecimal digits.
          term_push_cmd(c);
          if (term.cmd_len == 7) {
            uint n, r, g, b;
            sscanf(term.cmd_buf, "%1x%2x%2x%2x", &n, &r, &g, &b);
            win_set_colour(n, make_colour(r, g, b));
            term.state = NORMAL;
          }
        }
        else {
          // End of sequence. Put the character back unless the sequence was
          // terminated properly.
          term.state = NORMAL;
          if (c != '\a') {
            pos--;
            continue;
          }
        }

      when CMD_STRING:
        switch (c) {
          when '\a':
            do_cmd();
            term.state = NORMAL;
          when '\e':
            term.state = CMD_ESCAPE;
          when '\n' or '\r':
            // accept new lines in OSC strings
            if (term.cmd_num != 1337)
              term_push_cmd(c);
            // else ignore new lines in base64-encoded images
          otherwise:
            term_push_cmd(c);
        }

      when IGNORE_STRING:
        switch (c) {
          when '\a':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
          when '\n' or '\r':
            // keep IGNORE_STRING
            ;
        }

      when DCS_START:
        term.cmd_num = -1;
        term.cmd_len = 0;
        term.dcs_cmd = 0;
        // use csi_arg vars also for DCS parameters
        term.csi_argc = 0;
        memset(term.csi_argv, 0, sizeof(term.csi_argv));
        memset(term.csi_argv_defined, 0, sizeof(term.csi_argv_defined));

        switch (c) {
          when '@' ... '~':  /* DCS cmd final byte */
            term.dcs_cmd = c;
            do_dcs();
            term.state = DCS_PASSTHROUGH;
          when '\e':
            term.state = DCS_ESCAPE;
          when '0' ... '9':  /* DCS parameter */
            //printf("DCS start %c\n", c);
            term.state = DCS_PARAM;
          when ';':          /* DCS separator */
            //printf("DCS sep %c\n", c);
            term.state = DCS_PARAM;
          when ':':
            //printf("DCS sep %c\n", c);
            term.state = DCS_IGNORE;
          when '<' ... '?':
            term.dcs_cmd = c;
            //printf("DCS sep %c\n", c);
            term.state = DCS_PARAM;
          when ' ' ... '/':  /* DCS intermediate byte */
            term.dcs_cmd = c;
            term.state = DCS_INTERMEDIATE;
          otherwise:
            term.state = DCS_IGNORE;
        }

      when DCS_PARAM:
        switch (c) {
          when '@' ... '~':  /* DCS cmd final byte */
            term.dcs_cmd = term.dcs_cmd << 8 | c;
            if (term.csi_argv[term.csi_argc])
              term.csi_argc ++;
            do_dcs();
            term.state = DCS_PASSTHROUGH;
          when '\e':
            term.state = DCS_ESCAPE;
            term.esc_mod = 0;
          when '0' ... '9':  /* DCS parameter */
            //printf("DCS param %c\n", c);
            if (term.csi_argc < 2) {
              uint i = term.csi_argc;
              term.csi_argv[i] = 10 * term.csi_argv[i] + c - '0';
            }
          when ';' or ':':  /* DCS parameter separator */
            //printf("DCS param sep %c\n", c);
            if (term.csi_argc + 1 < lengthof(term.csi_argv))
              term.csi_argc ++;
          when '<' ... '?':
            term.dcs_cmd = term.dcs_cmd << 8 | c;
            //printf("DCS param %c\n", c);
            term.state = DCS_PARAM;
          when ' ' ... '/':  /* DCS intermediate byte */
            //printf("DCS param->inter %c\n", c);
            term.dcs_cmd = term.dcs_cmd << 8 | c;
            term.state = DCS_INTERMEDIATE;
          otherwise:
            term.state = DCS_IGNORE;
        }

      when DCS_INTERMEDIATE:
        switch (c) {
          when '@' ... '~':  /* DCS cmd final byte */
            term.dcs_cmd = term.dcs_cmd << 8 | c;
            do_dcs();
            term.state = DCS_PASSTHROUGH;
          when '\e':
            term.state = DCS_ESCAPE;
            term.esc_mod = 0;
          when '0' ... '?':  /* DCS parameter byte */
            //printf("DCS inter->ignore %c\n", c);
            term.state = DCS_IGNORE;
          when ' ' ... '/':  /* DCS intermediate byte */
            term.dcs_cmd = term.dcs_cmd << 8 | c;
          otherwise:
            term.state = DCS_IGNORE;
        }

      when DCS_PASSTHROUGH:
        switch (c) {
          when '\e':
            term.state = DCS_ESCAPE;
            term.esc_mod = 0;
          otherwise:
            if (!term_push_cmd(c)) {
              do_dcs();
              term.cmd_buf[0] = c;
              term.cmd_len = 1;
            }
        }

      when DCS_IGNORE:
        switch (c) {
          when '\e':
            term.state = ESCAPE;
            term.esc_mod = 0;
        }

      when DCS_ESCAPE:
        if (c < 0x20) {
          do_ctrl(c);
          term.state = NORMAL;
        } else if (c < 0x30) {
          term.esc_mod = term.esc_mod ? 0xFF : c;
          term.state = ESCAPE;
        } else if (c == '\\') {
          /* Process DCS sequence if we see ST. */
          do_dcs();
          term.state = NORMAL;
        } else {
          term.state = ESCAPE;
          term.imgs.parser_state = NULL;
          do_esc(c);
        }
    }
  }

  if (term.ring_enabled && term.curs.y != oldy)
    term.ring_enabled = false;

  if (cfg.ligatures_support > 1) {
    // refresh ligature rendering in old cursor line
    term_invalidate(0, oldy, term.cols - 1, oldy);
  }

  // Update search match highlighting
  //term_schedule_search_partial_update();
  term_schedule_search_update();

  // Update screen
  win_schedule_update();

  // Print
  if (term.printing) {
    printer_write(term.printbuf, term.printbuf_pos);
    term.printbuf_pos = 0;
  }
}

/* Empty the input buffer */
void
term_flush(void)
{
  if (term.suspbuf) {
    term_do_write(term.suspbuf, term.suspbuf_pos);
    free(term.suspbuf);
    term.suspbuf = 0;
    term.suspbuf_pos = 0;
    term.suspbuf_size = 0;
  }
}

void
term_write(const char *buf, uint len)
{
 /*
    During drag-selects, some people do not wish to process terminal output,
    because the user may want the screen to hold still to be selected.
    Therefore, we maintain a suspend-output-on-selection buffer which 
    can grow up to a configurable size.
  */
  if (term_selecting() && cfg.suspbuf_max > 0) {
    // || term.no_scroll ? -> more reliably handled in child_proc

    // if buffer size would be exceeded, flush; prevent uint overflow
    if (len > cfg.suspbuf_max - term.suspbuf_pos)
      term_flush();
    // if buffer length does not exceed max size, append output
    if (len <= cfg.suspbuf_max - term.suspbuf_pos) {
      // make sure buffer is large enough
      if (term.suspbuf_pos + len > term.suspbuf_size) {
        term.suspbuf_size = term.suspbuf_pos + len;
        term.suspbuf = renewn(term.suspbuf, term.suspbuf_size);
      }
      memcpy(term.suspbuf + term.suspbuf_pos, buf, len);
      term.suspbuf_pos += len;
      return;
    }
    // if we cannot buffer, output directly;
    // in this case, we've either flushed already or didn't need to
  }

  term_do_write(buf, len);
}

