// termout.c (part of mintty)
// Copyright 2008-09 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "linedisc.h"
#include "win.h"
#include "appinfo.h"
#include "charset.h"
#include "platform.h"

#include <sys/termios.h>

/*
 * Terminal emulator.
 */

#define ANSI(x,y)	((x)+((y)<<8))
#define ANSI_QUE(x)	ANSI(x,true)

#define compatibility(x) \
    if ( ((CL_##x)&term.compatibility_level) == 0 ) { \
       term.state=TOPLEVEL; \
       break; \
    }

static const char answerback[] = "mintty";
static const char primary_da[] = "\e[?1;2c";

 /* The vt100 linedraw characters.
  * Windows fonts don't tend to have the horizontal line characters at
  * different levels (0x23BA..0x23BD), hence they've been replaced
  * with approximations here (0x00af, 0x2500, 0x005f).
  */
static const wchar linedraw_chars[32] = {
  0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
  0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x00af,
  0x2500, 0x2500, 0x2500, 0x005f, 0x251c, 0x2524, 0x2534, 0x252c,
  0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
};

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void
move(int x, int y, int marg_clip)
{
  if (x < 0)
    x = 0;
  if (x >= term.cols)
    x = term.cols - 1;
  if (marg_clip) {
    if ((term.curs.y >= term.marg_t || marg_clip == 2) && y < term.marg_t)
      y = term.marg_t;
    if ((term.curs.y <= term.marg_b || marg_clip == 2) && y > term.marg_b)
      y = term.marg_b;
  }
  if (y < 0)
    y = 0;
  if (y >= term.rows)
    y = term.rows - 1;
  term.curs.x = x;
  term.curs.y = y;
  term.wrapnext = false;
}

static void
set_erase_char(void)
{
  term.erase_char = term.basic_erase_char;
  if (term.use_bce)
    term.erase_char.attr = (term.curr_attr & (ATTR_FGMASK | ATTR_BGMASK));
}

/*
 * Call this whenever the terminal window state changes, to queue
 * an update.
 */
static void
seen_disp_event(void)
{
  term.seen_disp_event = true;  /* for scrollback-reset-on-activity */
  win_schedule_update();
}

/*
 * Save the cursor and SGR mode.
 */
static void
save_cursor(void)
{
  term.savecurs = term.curs;
  term.save_attr = term.curr_attr;
  term.save_cset_i = term.cset_i;
  term.save_utf = term.utf;
  term.save_wnext = term.wrapnext;
  term.save_cset = term.csets[term.cset_i];
  term.save_oem_acs = term.oem_acs;
}

/*
 * Restore the cursor and SGR mode.
 */
static void
restore_cursor(void)
{
  term.curs = term.savecurs;
 /* Make sure the window hasn't shrunk since the save */
  if (term.curs.x >= term.cols)
    term.curs.x = term.cols - 1;
  if (term.curs.y >= term.rows)
    term.curs.y = term.rows - 1;

  term.curr_attr = term.save_attr;
  term.cset_i = term.save_cset_i;
  term.utf = term.save_utf;
  term.wrapnext = term.save_wnext;
 /*
  * wrapnext might reset to False if the x position is no
  * longer at the rightmost edge.
  */
  if (term.wrapnext && term.curs.x < term.cols - 1)
    term.wrapnext = false;
  term.csets[term.cset_i] = term.save_cset;
  term.oem_acs = term.save_oem_acs; 

  set_erase_char();
  term_update_cs();
  seen_disp_event();
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void
insert_char(int n)
{
  int dir = (n < 0 ? -1 : +1);
  int m, j;
  pos cursplus;
  termline *ldata;

  n = (n < 0 ? -n : n);
  if (n > term.cols - term.curs.x)
    n = term.cols - term.curs.x;
  m = term.cols - term.curs.x - n;
  cursplus.y = term.curs.y;
  cursplus.x = term.curs.x + n;
  term_check_boundary(term.curs.x, term.curs.y);
  if (dir < 0)
    term_check_boundary(term.curs.x + n, term.curs.y);
  ldata = lineptr(term.curs.y);
  if (dir < 0) {
    for (j = 0; j < m; j++)
      move_termchar(ldata, ldata->chars + term.curs.x + j,
                    ldata->chars + term.curs.x + j + n);
    while (n--)
      copy_termchar(ldata, term.curs.x + m++, &term.erase_char);
  }
  else {
    for (j = m; j--;)
      move_termchar(ldata, ldata->chars + term.curs.x + j + n,
                    ldata->chars + term.curs.x + j);
    while (n--)
      copy_termchar(ldata, term.curs.x + n, &term.erase_char);
  }
}

static void
write_bell(void)
{
  if (cfg.bell_flash)
    term_schedule_vbell(false, 0);
  win_bell();
}

static void
write_backspace(void)
{
  if (term.curs.x == 0 && (term.curs.y == 0 || term.wrap == 0))
   /* do nothing */ ;
  else if (term.curs.x == 0 && term.curs.y > 0)
    term.curs.x = term.cols - 1, term.curs.y--;
  else if (term.wrapnext)
    term.wrapnext = false;
  else
    term.curs.x--;
  seen_disp_event();
}

static void
write_tab(void)
{
  termline *ldata = lineptr(term.curs.y);
  do {
    term.curs.x++;
  } while (term.curs.x < term.cols - 1 && !term.tabs[term.curs.x]);
  
  if ((ldata->lattr & LATTR_MODE) != LATTR_NORM) {
    if (term.curs.x >= term.cols / 2)
      term.curs.x = term.cols / 2 - 1;
  }
  else {
    if (term.curs.x >= term.cols)
      term.curs.x = term.cols - 1;
  }
  
  seen_disp_event();
}

static void
write_return(void)
{
  term.curs.x = 0;
  term.wrapnext = false;
  seen_disp_event();
}

static void
write_linefeed(void)
{
  if (term.curs.y == term.marg_b)
    term_do_scroll(term.marg_t, term.marg_b, 1, true);
  else if (term.curs.y < term.rows - 1)
    term.curs.y++;
  term.wrapnext = false;
  seen_disp_event();
}

static bool
write_ctrl(char c)
{
  switch (c) {
    when '\e':   /* ESC: Escape */
      compatibility(ANSIMIN);
      term.state = SEEN_ESC;
      term.esc_query = false;
    when '\a':   /* BEL: Bell */
      write_bell();
    when '\b':     /* BS: Back space */
      write_backspace();
    when '\t':     /* HT: Character tabulation */
      write_tab();
    when '\v':   /* VT: Line tabulation */
      compatibility(VT100);
      write_linefeed();
    when '\f':   /* FF: Form feed */
      write_linefeed();
    when '\r':   /* CR: Carriage return */
      write_return();
    when '\n':   /* LF: Line feed */
      write_linefeed();
      if (term.newline_mode)
        write_return();
    when CTRL('E'):   /* ENQ: terminal type query */
      compatibility(ANSIMIN);
      ldisc_send(answerback, sizeof(answerback) - 1, 0);
    when CTRL('N'):   /* LS1: Locking-shift one */
      compatibility(VT100);
      term.cset_i = 1;
      term_update_cs();
    when CTRL('O'):   /* LS0: Locking-shift zero */
      compatibility(VT100);
      term.cset_i = 0;
      term_update_cs();
    otherwise:
      return false;
  }
  return true;
}

static void
write_char(wchar c, int width)
{
  termline *cline = lineptr(term.curs.y);
  void put_char(wchar c)
  {
    clear_cc(cline, term.curs.x);
    cline->chars[term.curs.x].chr = c;
    cline->chars[term.curs.x].attr = term.curr_attr;
  }  

  if (term.wrapnext && term.wrap && width > 0) {
    cline->lattr |= LATTR_WRAPPED;
    if (term.curs.y == term.marg_b)
      term_do_scroll(term.marg_t, term.marg_b, 1, true);
    else if (term.curs.y < term.rows - 1)
      term.curs.y++;
    term.curs.x = 0;
    term.wrapnext = false;
    cline = lineptr(term.curs.y);
  }
  if (term.insert && width > 0)
    insert_char(width);
  switch (width) {
    when 1:  // Normal character.
      term_check_boundary(term.curs.x, term.curs.y);
      term_check_boundary(term.curs.x + 1, term.curs.y);
      put_char(c);
    when 2:  // Double-width character.
     /*
      * If we're about to display a double-width
      * character starting in the rightmost
      * column, then we do something special
      * instead. We must print a space in the
      * last column of the screen, then wrap;
      * and we also set LATTR_WRAPPED2 which
      * instructs subsequent cut-and-pasting not
      * only to splice this line to the one
      * after it, but to ignore the space in the
      * last character position as well.
      * (Because what was actually output to the
      * terminal was presumably just a sequence
      * of CJK characters, and we don't want a
      * space to be pasted in the middle of
      * those just because they had the
      * misfortune to start in the wrong parity
      * column. xterm concurs.)
      */
      term_check_boundary(term.curs.x, term.curs.y);
      term_check_boundary(term.curs.x + 2, term.curs.y);
      if (term.curs.x == term.cols - 1) {
        copy_termchar(cline, term.curs.x, &term.erase_char);
        cline->lattr |= LATTR_WRAPPED | LATTR_WRAPPED2;
        if (term.curs.y == term.marg_b)
          term_do_scroll(term.marg_t, term.marg_b, 1, true);
        else if (term.curs.y < term.rows - 1)
          term.curs.y++;
        term.curs.x = 0;
        cline = lineptr(term.curs.y);
       /* Now we must term_check_boundary again, of course. */
        term_check_boundary(term.curs.x, term.curs.y);
        term_check_boundary(term.curs.x + 2, term.curs.y);
      }
      put_char(c);
      term.curs.x++;
      put_char(UCSWIDE);
    when 0:  // Combining character.
      if (term.curs.x > 0) {
        int x = term.curs.x - 1;
       /* If we're in wrapnext state, the character
        * to combine with is _here_, not to our left. */
        if (term.wrapnext)
          x++;
       /*
        * If the previous character is
        * UCSWIDE, back up another one.
        */
        if (cline->chars[x].chr == UCSWIDE) {
          assert(x > 0);
          x--;
        }
        add_cc(cline, x, c);
        seen_disp_event();
      }
      return;
    otherwise:  // Anything else. Probably shouldn't get here.
      return;
  }
  term.curs.x++;
  if (term.curs.x == term.cols) {
    term.curs.x--;
    term.wrapnext = true;
  }
  seen_disp_event();
}

static void
write_error(void)
{
  // Write 'Medium Shade' character from vt100 linedraw set,
  // which looks appropriately erroneous.
  write_char(0x2592, 1);
}

static void
do_esc(uchar c)
{
  term.state = TOPLEVEL;
  switch (ANSI(c, term.esc_query)) {
    when '[':  /* enter CSI mode */
      term.state = SEEN_CSI;
      term.esc_nargs = 1;
      term.esc_args[0] = ARG_DEFAULT;
      term.esc_query = false;
    when ']':  /* OSC: xterm escape sequences */
     /* Compatibility is nasty here, xterm, linux, decterm yuk! */
      compatibility(OTHER);
      term.state = SEEN_OSC;
      term.esc_args[0] = 0;
    when 'P':  /* DCS: Device Control String sequences */
      compatibility(VT100);
      term.state = SEEN_DCS;
    when '7':  /* DECSC: save cursor */
      compatibility(VT100);
      save_cursor();
    when '8':  /* DECRC: restore cursor */
      compatibility(VT100);
      restore_cursor();
    when '=':  /* DECKPAM: Keypad application mode */
      compatibility(VT100);
      term.app_keypad = true;
    when '>':  /* DECKPNM: Keypad numeric mode */
      compatibility(VT100);
      term.app_keypad = false;
    when 'D':  /* IND: exactly equivalent to LF */
      compatibility(VT100);
      write_linefeed();
    when 'E':  /* NEL: exactly equivalent to CR-LF */
      compatibility(VT100);
      write_return();
      write_linefeed();
    when 'M':  /* RI: reverse index - backwards LF */
      compatibility(VT100);
      if (term.curs.y == term.marg_t)
        term_do_scroll(term.marg_t, term.marg_b, -1, true);
      else if (term.curs.y > 0)
        term.curs.y--;
      term.wrapnext = false;
      seen_disp_event();
    when 'Z':  /* DECID: terminal type query */
      compatibility(VT100);
      ldisc_send(primary_da, sizeof primary_da - 1, 0);
    when 'c':  /* RIS: restore power-on settings */
      compatibility(VT100);
      term_reset();
      term_clear_scrollback();
      if (term.reset_132) {
        win_resize(term.rows, 80);
        term.reset_132 = 0;
      }
      ldisc_send(null, 0, 0);
      seen_disp_event();
    when 'H':  /* HTS: set a tab */
      compatibility(VT100);
      term.tabs[term.curs.x] = true;
    when ANSI('8', '#'):    /* DECALN: fills screen with Es :-) */
      compatibility(VT100);
      termline *ldata;
      for (int i = 0; i < term.rows; i++) {
        ldata = lineptr(i);
        for (int j = 0; j < term.cols; j++) {
          copy_termchar(ldata, j, &term.basic_erase_char);
          ldata->chars[j].chr = 'E';
        }
        ldata->lattr = LATTR_NORM;
      }
      term.disptop = 0;
      seen_disp_event();
    when ANSI('3', '#'):  /* DECDHL: 2*height, top */
      compatibility(VT100);
      lineptr(term.curs.y)->lattr = LATTR_TOP;
    when ANSI('4', '#'):  /* DECDHL: 2*height, bottom */
      compatibility(VT100);
      lineptr(term.curs.y)->lattr = LATTR_BOT;
    when ANSI('5', '#'):  /* DECSWL: normal */
      compatibility(VT100);
      lineptr(term.curs.y)->lattr = LATTR_NORM;
    when ANSI('6', '#'):  /* DECDWL: 2*width */
      compatibility(VT100);
      lineptr(term.curs.y)->lattr = LATTR_WIDE;
    when ANSI('A', '(') or ANSI('B', '(') or ANSI('0', '('):
     /* GZD4: G0 designate 94-set */
      compatibility(VT100);
      term.csets[0] = c;
      term_update_cs();
    when ANSI('U', '('):  /* G0: OEM character set */
      compatibility(OTHER);
      term.csets[0] = CSET_OEM;
      term_update_cs();
    when ANSI('A', ')') or ANSI('B', ')') or ANSI('0', ')'):
     /* G1D4: G1-designate 94-set */
      compatibility(VT100);
      term.csets[1] = c;
      term_update_cs();
    when ANSI('U', ')'): /* G1: OEM character set */
      compatibility(OTHER);
      term.csets[1] = CSET_OEM;
      term_update_cs();
    when ANSI('8', '%') or ANSI('G', '%'):
      compatibility(OTHER);
      term.utf = true;
      term_update_cs();
    when ANSI('@', '%'):
      compatibility(OTHER);
      term.utf = false;
      term_update_cs();
  }
}

static void
do_sgr(void)
{
 /* Set Graphics Rendition.
  *
  * A VT100 without the AVO only had one
  * attribute, either underline or
  * reverse video depending on the
  * cursor type, this was selected by
  * CSI 7m.
  *
  * when 2:
  *  This is sometimes DIM, eg on the
  *  GIGI and Linux
  * when 8:
  *  This is sometimes INVIS various ANSI.
  * when 21:
  *  This like 22 disables BOLD, DIM and INVIS
  *
  * The ANSI colours appear on any
  * terminal that has colour (obviously)
  * but the interaction between sgr0 and
  * the colours varies but is usually
  * related to the background colour
  * erase item. The interaction between
  * colour attributes and the mono ones
  * is also very implementation
  * dependent.
  *
  * The 39 and 49 attributes are likely
  * to be unimplemented.
  */
  int nargs = term.esc_nargs;
  for (int i = 0; i < nargs; i++) {
    switch (term.esc_args[i]) {
      when 0:  /* restore defaults */
        term.curr_attr = term.default_attr;
      when 1:  /* enable bold */
        compatibility(VT100AVO);
        term.curr_attr |= ATTR_BOLD;
      when 4 or 21:  /* enable underline */
        compatibility(VT100AVO);
        term.curr_attr |= ATTR_UNDER;
      when 5:  /* enable blink */
        compatibility(VT100AVO);
        term.curr_attr |= ATTR_BLINK;
      when 7:  /* enable reverse video */
        term.curr_attr |= ATTR_REVERSE;
      when 10: /* OEM acs off */
        compatibility(OTHER);
        term.oem_acs = 0;
        term_update_cs();
      when 11: /* OEM acs on */
        compatibility(OTHER);
        term.oem_acs = 1;
        term_update_cs();
      when 12: /* OEM acs on, |0x80 */
        compatibility(OTHER);
        term.oem_acs = 2;
        term_update_cs();
      when 22: /* disable bold */
        compatibility(VT220);
        term.curr_attr &= ~ATTR_BOLD;
      when 24: /* disable underline */
        compatibility(VT220);
        term.curr_attr &= ~ATTR_UNDER;
      when 25: /* disable blink */
        compatibility(VT220);
        term.curr_attr &= ~ATTR_BLINK;
      when 27: /* disable reverse video */
        compatibility(VT220);
        term.curr_attr &= ~ATTR_REVERSE;
      when 30 ... 37:
       /* foreground */
        term.curr_attr &= ~ATTR_FGMASK;
        term.curr_attr |=
          (term.esc_args[i] - 30) << ATTR_FGSHIFT;
      when 90 ... 97:
       /* aixterm-style bright foreground */
        term.curr_attr &= ~ATTR_FGMASK;
        term.curr_attr |= ((term.esc_args[i] - 90 + 8)
                           << ATTR_FGSHIFT);
      when 39: /* default-foreground */
        term.curr_attr &= ~ATTR_FGMASK;
        term.curr_attr |= ATTR_DEFFG;
      when 40 ... 47:
       /* background */
        term.curr_attr &= ~ATTR_BGMASK;
        term.curr_attr |=
          (term.esc_args[i] - 40) << ATTR_BGSHIFT;
      when 100 ... 107:
       /* aixterm-style bright background */
        term.curr_attr &= ~ATTR_BGMASK;
        term.curr_attr |= ((term.esc_args[i] - 100 + 8)
                           << ATTR_BGSHIFT);
      when 49: /* default-background */
        term.curr_attr &= ~ATTR_BGMASK;
        term.curr_attr |= ATTR_DEFBG;
      when 38: /* xterm 256-colour mode */
        if (i + 2 < nargs && term.esc_args[i + 1] == 5) {
          term.curr_attr &= ~ATTR_FGMASK;
          term.curr_attr |= ((term.esc_args[i + 2] & 0xFF)
                             << ATTR_FGSHIFT);
          i += 2;
        }
      when 48: /* xterm 256-colour mode */
        if (i + 2 < nargs && term.esc_args[i + 1] == 5) {
          term.curr_attr &= ~ATTR_BGMASK;
          term.curr_attr |= ((term.esc_args[i + 2] & 0xFF)
                             << ATTR_BGSHIFT);
          i += 2;
        }
    }
  }
  set_erase_char();
}

/*
 * Set terminal modes in escape arguments to state.
 */
static void
set_modes(bool state)
{
  for (int i = 0; i < term.esc_nargs; i++) {
    int mode = term.esc_args[i];
    if (term.esc_query) {
      switch (mode) {
        when 1:  /* DECCKM: application cursor keys */
          term.app_cursor_keys = state;
        when 2:  /* DECANM: VT52 mode */
          // IGNORE
        when 3:  /* DECCOLM: 80/132 columns */
          term.selected = false;
          win_resize(term.rows, state ? 132 : 80);
          term.reset_132 = state;
          term.alt_t = term.marg_t = 0;
          term.alt_b = term.marg_b = term.rows - 1;
          move(0, 0, 0);
          term_erase_lots(false, true, true);
        when 5:  /* DECSCNM: reverse video */
          if (state != term.rvideo) {
            term.rvideo = state;
            win_invalidate_all();
          }
        when 6:  /* DECOM: DEC origin mode */
          term.dec_om = state;
        when 7:  /* DECAWM: auto wrap */
          term.wrap = state;
        when 8:  /* DECARM: auto key repeat */
          // ignore
          //term.repeat_off = !state;
        when 9:  /* X10_MOUSE */
          term.mouse_mode = state ? MM_X10 : 0;
          win_update_mouse();
        when 10: /* DECEDM: set local edit mode */
          term.editing = state;
          ldisc_send(null, 0, 0);
        when 25: /* DECTCEM: enable/disable cursor */
          compatibility(VT220);
          term.cursor_on = state;
          seen_disp_event();
        when 47: /* alternate screen */
          compatibility(OTHER);
          term.selected = false;
          term_swap_screen(state, false, false);
          term.disptop = 0;
        when 67: /* DECBKM: backarrow key mode */
          compatibility(VT420);
          term.backspace_sends_bs = state;
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
        when 1047:       /* alternate screen */
          compatibility(OTHER);
          term.selected = false;
          term_swap_screen(state, true, true);
          term.disptop = 0;
        when 1048:       /* save/restore cursor */
          if (state)
            save_cursor();
          else
            restore_cursor();
        when 1049:       /* cursor & alternate screen */
          if (state)
            save_cursor();
          compatibility(OTHER);
          term.selected = false;
          term_swap_screen(state, true, false);
          if (!state)
            restore_cursor();
          term.disptop = 0;
        when 7700:       /* mintty only: CJK ambigous width reporting */
          compatibility(OTHER);
          term.report_ambig_width = state;
        when 7727:       /* mintty only: Application escape key mode */
          compatibility(OTHER);
          term.app_escape_key = state;
        when 7728:       /* mintty only: Escape sends FS (instead of ESC) */
          compatibility(OTHER);
          term.escape_sends_fs = state;
        when 7787:       /* mintty only: Application mousewheel mode */
          compatibility(OTHER);
          term.app_wheel = state;
      }
    }
    else {
      switch (mode) {
        when 4:  /* IRM: set insert mode */
          compatibility(VT102);
          term.insert = state;
        when 12: /* SRM: set echo mode */
          term.echoing = !state;
          ldisc_send(null, 0, 0);
        when 20: /* LNM: Return sends ... */
          term.newline_mode = state;
      }
    }
  }
}

static void
do_csi(uchar c)
{
  int arg0 = term.esc_args[0], arg1 = term.esc_args[1];
  int def_arg0 = arg0 ?: 1;  // first arg with default
  int nargs = term.esc_nargs;
  switch (ANSI(c, term.esc_query)) {
    when 'A':        /* CUU: move up N lines */
      move(term.curs.x, term.curs.y - def_arg0, 1);
      seen_disp_event();
    when 'e':        /* VPR: move down N lines */
      compatibility(ANSI);
      move(term.curs.x, term.curs.y + def_arg0, 1);
      seen_disp_event();
    when 'B':        /* CUD: Cursor down */
      move(term.curs.x, term.curs.y + def_arg0, 1);
      seen_disp_event();
    when ANSI('c', '>'):     /* DA: report version */
      compatibility(OTHER);
      /* Terminal type 77 (ASCII 'M' for mintty) */
      if (!nargs || (nargs == 1 && arg0 == 0))
        ldisc_printf(0, "\e[>77;%u;0c", DECIMAL_VERSION);
    when 'a':        /* HPR: move right N cols */
      compatibility(ANSI);
      move(term.curs.x + def_arg0, term.curs.y, 1);
      seen_disp_event();
    when 'C':        /* CUF: Cursor right */
      move(term.curs.x + def_arg0, term.curs.y, 1);
      seen_disp_event();
    when 'D':        /* CUB: move left N cols */
      move(term.curs.x - def_arg0, term.curs.y, 1);
      seen_disp_event();
    when 'E':        /* CNL: move down N lines and CR */
      compatibility(ANSI);
      move(0, term.curs.y + def_arg0, 1);
      seen_disp_event();
    when 'F':        /* CPL: move up N lines and CR */
      compatibility(ANSI);
      move(0, term.curs.y - def_arg0, 1);
      seen_disp_event();
    when 'G' or '`':  /* CHA or HPA: set horizontal posn */
      compatibility(ANSI);
      move(def_arg0 - 1, term.curs.y, 0);
      seen_disp_event();
    when 'd':        /* VPA: set vertical posn */
      compatibility(ANSI);
      move(term.curs.x,
           ((term.dec_om ? term.marg_t : 0) +
            def_arg0 - 1), (term.dec_om ? 2 : 0));
      seen_disp_event();
    when 'H' or 'f':  /* CUP or HVP: set horz and vert posns at once */
      if (nargs < 2)
        arg1 = ARG_DEFAULT;
      move((arg1 ?: 1) - 1,
           ((term.dec_om ? term.marg_t : 0) +
            def_arg0 - 1), (term.dec_om ? 2 : 0));
      seen_disp_event();
    when 'J': {      /* ED: erase screen or parts of it */
      if (arg0 == 3) /* Erase Saved Lines (xterm) */
        term_clear_scrollback();
      else {
        bool below = arg0 == 0 || arg0 == 2;
        bool above = arg0 == 1 || arg0 == 2;
        term_erase_lots(false, above, below);
      }
      term.disptop = 0;
      seen_disp_event();
    }
    when 'K': {      /* EL: erase line or parts of it */
      bool right = arg0 == 0 || arg0 == 2;
      bool left  = arg0 == 1 || arg0 == 2;
      term_erase_lots(true, left, right);
      seen_disp_event();
    }
    when 'L':        /* IL: insert lines */
      compatibility(VT102);
      if (term.curs.y <= term.marg_b) {
        term_do_scroll(term.curs.y, term.marg_b,
                       -def_arg0, false);
      }
      seen_disp_event();
    when 'M':        /* DL: delete lines */
      compatibility(VT102);
      if (term.curs.y <= term.marg_b) {
        term_do_scroll(term.curs.y, term.marg_b,
                       def_arg0, true);
      }
      seen_disp_event();
    when '@':        /* ICH: insert chars */
     /* XXX VTTEST says this is vt220, vt510 manual says vt102 */
      compatibility(VT102);
      insert_char(def_arg0);
      seen_disp_event();
    when 'P':        /* DCH: delete chars */
      compatibility(VT102);
      insert_char(-def_arg0);
      seen_disp_event();
    when 'c':        /* DA: terminal type query */
      compatibility(VT100);
      ldisc_send(primary_da, sizeof primary_da - 1, 0);
    when 'n':        /* DSR: cursor position query */
      if (arg0 == 6)
        ldisc_printf(0, "\e[%d;%dR", term.curs.y + 1, term.curs.x + 1);
      else if (arg0 == 5) {
        ldisc_send("\e[0n", 4, 0);
      }
    when 'h' or ANSI_QUE('h'):  /* SM: toggle modes to high */
      compatibility(VT100);
      set_modes(true);
    when 'l' or ANSI_QUE('l'):  /* RM: toggle modes to low */
      compatibility(VT100);
      set_modes(false);
    when 'i' or ANSI_QUE('i'):  /* MC: Media copy */
      compatibility(VT100);
      if (nargs == 1) {
        if (arg0 == 5 && *cfg.printer) {
          term.printing = true;
          term.only_printing = !term.esc_query;
          term.print_state = 0;
          term_print_setup();
        }
        else if (arg0 == 4 && term.printing)
          term_print_finish();
      }
    when 'g':        /* TBC: clear tabs */
      compatibility(VT100);
      if (nargs == 1) {
        if (arg0 == 0) {
          term.tabs[term.curs.x] = false;
        }
        else if (arg0 == 3) {
          int i;
          for (i = 0; i < term.cols; i++)
            term.tabs[i] = false;
        }
      }
    when 'r':        /* DECSTBM: set scroll margins */
      compatibility(VT100);
      if (nargs <= 2) {
        int top = def_arg0 - 1;
        int bot = (
          nargs <= 1 || arg1 == 0
          ? term.rows 
          : (arg1 ?: term.rows)
        ) - 1;
        if (bot >= term.rows)
          bot = term.rows - 1;
       /* VTTEST Bug 9 - if region is less than 2 lines
        * don't change region.
        */
        if (bot - top > 0) {
          term.marg_t = top;
          term.marg_b = bot;
          term.curs.x = 0;
         /*
          * I used to think the cursor should be
          * placed at the top of the newly marginned
          * area. Apparently not: VMS TPU falls over
          * if so.
          *
          * Well actually it should for
          * Origin mode - RDB
          */
          term.curs.y = (term.dec_om ? term.marg_t : 0);
          seen_disp_event();
        }
      }
    when 'm':      /* SGR: set graphics rendition */
      do_sgr();
    when 's':        /* save cursor */
      save_cursor();
    when 'u':        /* restore cursor */
      restore_cursor();
    when 't': {      /* DECSLPP: set page size - ie window height */
     /*
      * VT340/VT420 sequence DECSLPP, DEC only allows values
      *  24/25/36/48/72/144 other emulators (eg dtterm) use
      * illegal values (eg first arg 1..9) for window changing 
      * and reports.
      */
      if (nargs <= 1 && (arg0 < 1 || arg0 >= 24)) {
        compatibility(VT340TEXT);
        win_resize((arg0 ?: 24), term.cols);
        term.selected = false;
      }
      else if (nargs >= 1 && arg0 >= 1 && arg0 < 24) {
        compatibility(OTHER);
        int x, y;
        switch (arg0) {
          when 1: win_set_iconic(false);
          when 2: win_set_iconic(true);
          when 3:
            if (nargs >= 3)
              win_move(arg1, term.esc_args[2]);
          when 4:
           /* We should resize the window to a given
            * size in pixels here, but currently our
            * resizing code isn't healthy enough to
            * manage it. */
          when 5:
           /* move to top */
            win_set_zorder(true);
          when 6:
           /* move to bottom */
            win_set_zorder(false);
          when 8:
            if (nargs >= 3) {
              win_resize(arg1 ?: cfg.rows,
                         term.esc_args[2] ?: cfg.cols);
            }
          when 9:
            if (nargs >= 2)
              win_set_zoom(arg1 != 0);
          when 11:
            ldisc_send(win_is_iconic() ? "\e[1t" : "\e[2t", 4, 0);
          when 13:
            win_get_pos(&x, &y);
            ldisc_printf(0, "\e[3;%d;%dt", x, y);
          when 14:
            win_get_pixels(&x, &y);
            ldisc_printf(0, "\e[4;%d;%dt", x, y);
          when 18:
            ldisc_printf(0, "\e[8;%d;%dt", term.rows, term.cols);
          when 19:
           /*
            * Hmmm. Strictly speaking we
            * should return `the size of the
            * screen in characters', but
            * that's not easy: (a) window
            * furniture being what it is it's
            * hard to compute, and (b) in
            * resize-font mode maximising the
            * window wouldn't change the
            * number of characters. *shrug*. I
            * think we'll ignore it for the
            * moment and see if anyone
            * complains, and then ask them
            * what they would like it to do.
            */
          when 20 or 21:
            ldisc_send("\e]l\e\\", 5, 0);
        }
      }
    }
    when 'S':        /* SU: Scroll up */
      compatibility(VT340TEXT);
      term_do_scroll(term.marg_t, term.marg_b,
                     def_arg0, true);
      term.wrapnext = false;
      seen_disp_event();
    when 'T':        /* SD: Scroll down */
      compatibility(VT340TEXT);
      /* Avoid clash with hilight mouse tracking mode sequence */
      if (nargs <= 1) {
        term_do_scroll(term.marg_t, term.marg_b, -def_arg0, true);
        term.wrapnext = false;
        seen_disp_event();
      }
    when ANSI('|', '*'):     /* DECSNLS */
     /* 
      * Set number of lines on screen
      * VT420 uses VGA like hardware and can
      * support any size in reasonable range
      * (24..49 AIUI) with no default specified.
      */
      compatibility(VT420);
      if (nargs == 1 && arg0 > 0) {
        win_resize(arg0 ?: cfg.rows, term.cols);
        term.selected = false;
      }
    when ANSI('|', '$'):     /* DECSCPP */
     /*
      * Set number of columns per page
      * Docs imply range is only 80 or 132, but
      * I'll allow any.
      */
      compatibility(VT340TEXT);
      if (nargs <= 1) {
        win_resize(term.rows, arg0 ?: cfg.cols);
        term.selected = false;
      }
    when 'X': {      /* ECH: write N spaces w/o moving cursor */
     /* XXX VTTEST says this is vt220, vt510 manual
      * says vt100 */
      compatibility(ANSIMIN);
      int n = def_arg0;
      pos cursplus;
      int p = term.curs.x;
      termline *cline = lineptr(term.curs.y);

      if (n > term.cols - term.curs.x)
        n = term.cols - term.curs.x;
      cursplus = term.curs;
      cursplus.x += n;
      term_check_boundary(term.curs.x, term.curs.y);
      term_check_boundary(term.curs.x + n, term.curs.y);
      while (n--)
        copy_termchar(cline, p++, &term.erase_char);
      seen_disp_event();
    }
    when 'x': {      /* DECREQTPARM: report terminal characteristics */
      compatibility(VT100);
      if (arg0 <= 1)
        ldisc_printf(0, "\e[%c;1;1;112;112;1;0x", '2' + arg0);
    }
    when 'Z': {       /* CBT */
      compatibility(OTHER);
      int i = def_arg0; 
      while (--i >= 0 && term.curs.x > 0) {
        do {
          term.curs.x--;
        } while (term.curs.x > 0 && !term.tabs[term.curs.x]);
      }
    }
    when ANSI('p', '"'): {   /* DECSCL: set compat level */
     /*
      * Allow the host to make this emulator a
      * 'perfect' VT102. This first appeared in
      * the VT220, but we do need to get back to
      * PuTTY mode so I won't check it.
      *
      * The arg in 40..42,50 are a PuTTY extension.
      * The 2nd arg, 8bit vs 7bit is not checked.
      *
      * Setting VT102 mode should also change
      * the Fkeys to generate PF* codes as a
      * real VT102 has no Fkeys. The VT220 does
      * this, F11..F13 become ESC,BS,LF other
      * Fkeys send nothing.
      *
      * Note ESC c will NOT change this!
      */
      switch (arg0) {
        when 61:
          term.compatibility_level &= ~TM_VTXXX;
          term.compatibility_level |= TM_VT102;
        when 62:
          term.compatibility_level &= ~TM_VTXXX;
          term.compatibility_level |= TM_VT220;
        when 40:
          term.compatibility_level &= TM_VTXXX;
        when 41:
          term.compatibility_level = TM_MINTTY;
        when ARG_DEFAULT:
          term.compatibility_level = TM_MINTTY;
        when 50: // ignore
        otherwise:
          if (arg0 > 60 && arg0 < 70)
            term.compatibility_level |= TM_VTXXX;
      }
    }
    when ANSI('m', '>'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      compatibility(OTHER);
      if (!nargs)
        term.modify_other_keys = 0;
      else if (arg0 == 4)
        term.modify_other_keys = nargs > 1 ? arg1 : 0;
    when ANSI('n', '>'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      compatibility(OTHER);
      if (nargs == 1 && arg0 == 4)
        term.modify_other_keys = 0;
    when ANSI('q', ' '):     /* DECSCUSR: set cursor style */
      compatibility(VT510);
      if (nargs == 1) {
        term.cursor_type = arg0 ? (arg0 - 1) / 2 : -1;
        term.cursor_blinks = arg0 ? arg0 % 2 : -1;
        term_schedule_cblink();
      }
   }
}

static colour
rgb_to_colour(uint32 rgb)
{
  return make_colour(rgb >> 16, rgb >> 8, rgb);
}

static void
do_colour_osc(uint i)
{
  char *s = term.osc_string;
  bool has_index_arg = !i;
  if (has_index_arg) {
    int len = 0;
    sscanf(s, "%u;%n", &i, &len);
    if (!len || i >= 262)
      return;
    s += len;
  }
  uint rgb, r, g, b;
  if (strcmp(s, "?") == 0) {
    ldisc_printf(0, "\e]%u;", term.esc_args[0]);
    if (has_index_arg)
      ldisc_printf(0, "%u;", i);
    uint c = win_get_colour(i);
    r = red(c), g = green(c), b = blue(c);
    ldisc_printf(0, "rgb:%04x/%04x/%04x\e\\", r * 0x101, g * 0x101, b * 0x101);
  }
  else if (sscanf(s, "#%6x%c", &rgb, &(char){0}) == 1)
    win_set_colour(i, rgb_to_colour(rgb));
  else if (sscanf(s, "rgb:%2x/%2x/%2x%c", &r, &g, &b, &(char){0}) == 3)
    win_set_colour(i, make_colour(r, g, b));
  else if (sscanf(s, "rgb:%4x/%4x/%4x%c", &r, &g, &b, &(char){0}) == 3)
    win_set_colour(i, make_colour(r >> 8, g >> 8, b >> 8));
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void
do_osc(void)
{
  if (!term.osc_w) { // "wordness" is ignored
    term.osc_string[term.osc_strlen] = 0;
    switch (term.esc_args[0]) {
      when 0 or 2 or 21: win_set_title(term.osc_string);  // ignore icon title
      when 4:  do_colour_osc(0);
      when 10: do_colour_osc(FG_COLOUR_I);
      when 11: do_colour_osc(BG_COLOUR_I);
      when 12: do_colour_osc(CURSOR_COLOUR_I);
    }
  }
}

void
term_write(const char *data, int len)
{
  bufchain_add(term.inbuf, data, len);

  if (term.in_term_write)
    return;
    
  // Reset cursor blinking.
  seen_disp_event();
  term.cblinker = 1;
  term_schedule_cblink();

 /*
  * During drag-selects, we do not process terminal input,
  * because the user will want the screen to hold still to
  * be selected.
  */
  if (term_selecting())
    return;
    
 /*
  * Remove everything currently in `inbuf' and stick it up on the
  * in-memory display. There's a big state machine in here to
  * process escape sequences...
  */
  term.in_term_write = true;
  int unget = -1;
  uchar *chars = 0;
  int nchars = 0;
  while (nchars > 0 || unget != -1 || bufchain_size(term.inbuf) > 0) {
    uchar c;
    if (unget == -1) {
      if (nchars == 0) {
        void *ret;
        bufchain_prefix(term.inbuf, &ret, &nchars);
        uchar localbuf[256];
        nchars = min(nchars, (int) sizeof localbuf);
        memcpy(localbuf, ret, nchars);
        bufchain_consume(term.inbuf, nchars);
        chars = localbuf;
        assert(chars != null);
      }
      c = *chars++;
      nchars--;
    }
    else {
      c = unget;
      unget = -1;
    }

   /* Note only VT220+ are 8-bit VT102 is seven bit, it shouldn't even
    * be able to display 8-bit characters, but I'll let that go 'cause
    * of i18n.
    */

   /*
    * If we're printing, add the character to the printer
    * buffer.
    */
    if (term.printing) {
      bufchain_add(term.printer_buf, &c, 1);

     /*
      * If we're in print-only mode, we use a much simpler
      * state machine designed only to recognise the ESC[4i
      * termination sequence.
      */
      if (term.only_printing) {
        if (c == '\e')
          term.print_state = 1;
        else if (c == '[' && term.print_state == 1)
          term.print_state = 2;
        else if (c == '4' && term.print_state == 2)
          term.print_state = 3;
        else if (c == 'i' && term.print_state == 3)
          term.print_state = 4;
        else
          term.print_state = 0;
        if (term.print_state == 4) {
          term_print_finish();
        }
        continue;
      }
    }

    switch (term.state) {
      when TOPLEVEL: {
        
        wchar wc;

        if (term.oem_acs && !memchr("\e\n\r\b", c, 4)) {
          if (term.oem_acs == 2)
            c |= 0x80;
          write_char(cs_btowc_glyph(c), 1);
          continue;
        }
        
        switch (cs_mb1towc(&wc, (char *)&c)) {
          when 0: // NUL or low surrogate
            if (wc)
              unget = c;
          when -1: // Encoding error
            cs_mb1towc(0, 0); // Clear decoder state
            write_error();
            if (term.in_mb_char)
              unget = c;
            term.in_mb_char = false;
            continue;
          when -2: // Incomplete character
            term.in_mb_char = true;
            continue;
        }
        
        term.in_mb_char = false;
        
        // Fetch previous high surrogate 
        wchar hwc = term.high_surrogate;
        term.high_surrogate = 0;
        
        // Low surrogate
        if ((wc & 0xFC00) == 0xDC00) {
          if (hwc) {
            #if HAS_LOCALES
            int width = wcswidth((wchar[]){hwc, wc}, 2);
            #else
            xchar xc = 0x10000 + ((hwc & 0x3FF) << 10 | (wc & 0x3FF));
            int width = xcwidth(xc);
            #endif
            write_char(hwc, width);
            write_char(wc, 0);
          }
          else
            write_error();
          continue;
        }
        
        if (hwc) // Previous high surrogate not followed by low one
          write_error();
        
        // High surrogate
        if ((wc & 0xFC00) == 0xD800) {
          term.high_surrogate = wc;
          continue;
        }
        
        // Control characters
        if (wc < 0x20 || wc == 0x7F) {
          if (!write_ctrl(wc) && c == wc) {
            wc = cs_btowc_glyph(c);
            if (wc != c)
              write_char(wc, 1);
          }
          continue;
        }

        // Everything else
        #if HAS_LOCALES
        int width = wcwidth(wc);
        #else
        int width = xcwidth(wc);
        #endif
        
        switch(term.csets[term.cset_i]) {
          when CSET_LINEDRW:
            if (0x60 <= wc && wc < 0x80)
              wc = linedraw_chars[wc - 0x60];
          when CSET_GBCHR:
            if (c == '#')
              wc = 0xA3; // pound sign
        }
        if (wc == 0x2010) {
         /* Many Windows fonts don't have the Unicode hyphen, but groff
          * uses it for man pages, so replace it with the ASCII version.
          */
          wc = '-';
        }
        write_char(wc, width);
      }
      when SEEN_ESC or OSC_MAYBE_ST or DCS_MAYBE_ST:
       /*
        * OSC_MAYBE_ST is virtually identical to SEEN_ESC, with the
        * exception that we have an OSC sequence in the pipeline,
        * and _if_ we see a backslash, we process it.
        */
        if (c == '\\' && term.state != SEEN_ESC) {
          if (term.state == OSC_MAYBE_ST)
            do_osc();
          term.state = TOPLEVEL;
        }
        else if (c >= ' ' && c <= '/') {
          if (term.esc_query)
            term.esc_query = -1;
          else
            term.esc_query = c;
        }
        else
          do_esc(c);
      when SEEN_CSI:
        if (isdigit(c)) {
          if (term.esc_nargs <= ARGS_MAX) {
            if (term.esc_args[term.esc_nargs - 1] == ARG_DEFAULT)
              term.esc_args[term.esc_nargs - 1] = 0;
            term.esc_args[term.esc_nargs - 1] =
              10 * term.esc_args[term.esc_nargs - 1] + c - '0';
          }
        }
        else if (c == ';') {
          if (++term.esc_nargs <= ARGS_MAX)
            term.esc_args[term.esc_nargs - 1] = ARG_DEFAULT;
        }
        else if (c < '@') {
          if (term.esc_query)
            term.esc_query = -1;
          else if (c == '?')
            term.esc_query = true;
          else
            term.esc_query = c;
        }
        else {
          do_csi(c);
          term.state = TOPLEVEL;
        }
      when SEEN_OSC: {
        term.osc_w = false;
        switch (c) {
          when 'P':  /* Linux palette sequence */
            term.state = SEEN_OSC_P;
            term.osc_strlen = 0;
          when 'R':  /* Linux palette reset */
            win_reset_colours();
            term.state = TOPLEVEL;
          when 'W':  /* word-set */
            term.state = SEEN_OSC_W;
            term.osc_w = true;
          when '0' ... '9':
            term.esc_args[0] = 10 * term.esc_args[0] + c - '0';
          otherwise: {
            if (c == 'L' && term.esc_args[0] == 2) {
              // Grotty hack to support xterm and DECterm title
              // sequences concurrently.
              term.esc_args[0] = 1;
            }
            else {
              term.state = OSC_STRING;
              term.osc_strlen = 0;
            }
          }
        }
      }
      when OSC_STRING: {
       /*
        * This OSC stuff is EVIL. It takes just one character to get into
        * sysline mode and it's not initially obvious how to get out.
        * So I've added CR and LF as string aborts.
        * This shouldn't effect compatibility as I believe embedded 
        * control characters are supposed to be interpreted (maybe?) 
        * and they don't display anything useful anyway.
        *
        * -- RDB
        */
        switch (c) {
         /*
          * These characters terminate the string; ST and BEL
          * terminate the sequence and trigger instant
          * processing of it, whereas ESC goes back to SEEN_ESC
          * mode unless it is followed by \, in which case it is
          * synonymous with ST in the first place.
          */
          when '\n' or '\r':
            term.state = TOPLEVEL;
          when '\a':
            do_osc();
            term.state = TOPLEVEL;
          when '\e':
            term.state = OSC_MAYBE_ST;
          otherwise:
            if (term.osc_strlen < OSC_STR_MAX)
              term.osc_string[term.osc_strlen++] = c;
        }
      }
      when SEEN_OSC_P: {
        if (!isxdigit(c)) {
          term.state = TOPLEVEL;
          break;
        }
        term.osc_string[term.osc_strlen++] = c;
        if (term.osc_strlen == 7) {
          uint n, rgb;
          sscanf(term.osc_string, "%1x%6x", &n, &rgb);
          win_set_colour(n, rgb_to_colour(rgb));
          term.state = TOPLEVEL;
        }
      }
      when SEEN_OSC_W:
        if ('0' <= c && c <= '9')
          term.esc_args[0] = 10 * term.esc_args[0] + c - '0';
        else {
          term.state = OSC_STRING;
          term.osc_strlen = 0;
        }
      when SEEN_DCS: {
       /* Parse and ignore Device Control String (DCS) sequences */
        switch (c) {
          when '\n' or '\r' or '\a':
            term.state = TOPLEVEL;
          when '\e':
            term.state = DCS_MAYBE_ST;
        }
      }
    }
  }
  term.in_term_write = false;
  term_print_flush();
}
