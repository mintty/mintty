// termout.c (part of mintty)
// Copyright 2008-10 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "linedisc.h"
#include "win.h"
#include "appinfo.h"
#include "charset.h"

#include <sys/termios.h>

/*
 * Terminal emulator.
 */

#define ANSI(x,y)	((x)+((y)<<8))
#define ANSI_QUE(x)	ANSI(x,true)

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
  term_screen *screen = &term.screen;
  term_cursor *curs = &screen->curs;
  if (x < 0)
    x = 0;
  if (x >= term.cols)
    x = term.cols - 1;
  if (marg_clip) {
    if ((curs->y >= screen->marg_t || marg_clip == 2) && y < screen->marg_t)
      y = screen->marg_t;
    if ((curs->y <= screen->marg_b || marg_clip == 2) && y > screen->marg_b)
      y = screen->marg_b;
  }
  if (y < 0)
    y = 0;
  if (y >= term.rows)
    y = term.rows - 1;
  curs->x = x;
  curs->y = y;
  curs->wrapnext = false;
}

static void
set_erase_char(void)
{
  term.erase_char = basic_erase_char;
  if (term.use_bce)
    term.erase_char.attr = term.screen.curs.attr & (ATTR_FGMASK | ATTR_BGMASK);
}

/*
 * Save the cursor and SGR mode.
 */
static void
save_cursor(void)
{
  term.screen.saved_curs = term.screen.curs;
}

/*
 * Restore the cursor and SGR mode.
 */
static void
restore_cursor(void)
{
  term_cursor *curs = &term.screen.curs;
  *curs = term.screen.saved_curs;
  
 /* Make sure the window hasn't shrunk since the save */
  if (curs->x >= term.cols)
    curs->x = term.cols - 1;
  if (curs->y >= term.rows)
    curs->y = term.rows - 1;

 /*
  * wrapnext might reset to False if the x position is no
  * longer at the rightmost edge.
  */
  if (curs->wrapnext && curs->x < term.cols - 1)
    curs->wrapnext = false;

  term_update_cs();
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void
insert_char(int n)
{
  int dir = (n < 0 ? -1 : +1);
  int m;
  termline *line;
  term_cursor *curs = &term.screen.curs;

  n = (n < 0 ? -n : n);
  if (n > term.cols - curs->x)
    n = term.cols - curs->x;
  m = term.cols - curs->x - n;
  term_check_boundary(curs->x, curs->y);
  if (dir < 0)
    term_check_boundary(curs->x + n, curs->y);
  line = term.screen.lines[curs->y];
  if (dir < 0) {
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
  term_cursor *curs = &term.screen.curs;
  if (curs->x == 0 && (curs->y == 0 || !term.screen.autowrap))
   /* do nothing */ ;
  else if (curs->x == 0 && curs->y > 0)
    curs->x = term.cols - 1, curs->y--;
  else if (curs->wrapnext)
    curs->wrapnext = false;
  else
    curs->x--;
}

static void
write_tab(void)
{
  term_cursor *curs = &term.screen.curs;
  termline *line = term.screen.lines[curs->y];
  do
    curs->x++;
  while (curs->x < term.cols - 1 && !term.tabs[curs->x]);
  
  if ((line->attr & LATTR_MODE) != LATTR_NORM) {
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
  term.screen.curs.x = 0;
  term.screen.curs.wrapnext = false;
}

static void
write_linefeed(void)
{
  term_cursor *curs = &term.screen.curs;
  if (curs->y == term.screen.marg_b)
    term_do_scroll(term.screen.marg_t, term.screen.marg_b, 1, true);
  else if (curs->y < term.rows - 1)
    curs->y++;
  curs->wrapnext = false;
}

static bool
write_ctrl(char c)
{
  switch (c) {
    when '\e':   /* ESC: Escape */
      term.state = SEEN_ESC;
      term.esc_query = false;
    when '\a':   /* BEL: Bell */
      write_bell();
    when '\b':     /* BS: Back space */
      write_backspace();
    when '\t':     /* HT: Character tabulation */
      write_tab();
    when '\v':   /* VT: Line tabulation */
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
      ldisc_send(cfg.answerback, strlen(cfg.answerback), false);
    when CTRL('N'):   /* LS1: Locking-shift one */
      term.screen.curs.g1 = true;
      term_update_cs();
    when CTRL('O'):   /* LS0: Locking-shift zero */
      term.screen.curs.g1 = false;
      term_update_cs();
    otherwise:
      return false;
  }
  return true;
}

static void
write_char(wchar c, int width)
{
  term_screen *screen = &term.screen;
  term_cursor *curs = &screen->curs;
  termline *line = screen->lines[curs->y];
  void put_char(wchar c)
  {
    clear_cc(line, curs->x);
    line->chars[curs->x].chr = c;
    line->chars[curs->x].attr = curs->attr;
  }  

  if (curs->wrapnext && screen->autowrap && width > 0) {
    line->attr |= LATTR_WRAPPED;
    if (curs->y == screen->marg_b)
      term_do_scroll(screen->marg_t, screen->marg_b, 1, true);
    else if (curs->y < term.rows - 1)
      curs->y++;
    curs->x = 0;
    curs->wrapnext = false;
    line = screen->lines[curs->y];
  }
  if (screen->insert && width > 0)
    insert_char(width);
  switch (width) {
    when 1:  // Normal character.
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + 1, curs->y);
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
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + 2, curs->y);
      if (curs->x == term.cols - 1) {
        line->chars[curs->x] = term.erase_char;
        line->attr |= LATTR_WRAPPED | LATTR_WRAPPED2;
        if (curs->y == screen->marg_b)
          term_do_scroll(screen->marg_t, screen->marg_b, 1, true);
        else if (curs->y < term.rows - 1)
          curs->y++;
        curs->x = 0;
        line = screen->lines[curs->y];
       /* Now we must term_check_boundary again, of course. */
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + 2, curs->y);
      }
      put_char(c);
      curs->x++;
      put_char(UCSWIDE);
    when 0:  // Combining character.
      if (curs->x > 0) {
        int x = curs->x - 1;
       /* If we're in wrapnext state, the character
        * to combine with is _here_, not to our left. */
        if (curs->wrapnext)
          x++;
       /*
        * If the previous character is
        * UCSWIDE, back up another one.
        */
        if (line->chars[x].chr == UCSWIDE) {
          assert(x > 0);
          x--;
        }
        add_cc(line, x, c);
      }
      return;
    otherwise:  // Anything else. Probably shouldn't get here.
      return;
  }
  curs->x++;
  if (curs->x == term.cols) {
    curs->x--;
    curs->wrapnext = true;
  }
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
  term_screen *screen = &term.screen;
  term_cursor *curs = &screen->curs;
  term.state = TOPLEVEL;
  switch (ANSI(c, term.esc_query)) {
    when '[':  /* enter CSI mode */
      term.state = SEEN_CSI;
      term.esc_nargs = 1;
      term.esc_args[0] = ARG_DEFAULT;
      term.esc_query = false;
    when ']':  /* OSC: xterm escape sequences */
      term.state = SEEN_OSC;
      term.esc_args[0] = 0;
    when 'P':  /* DCS: Device Control String sequences */
      term.state = SEEN_DCS;
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
      write_return();
      write_linefeed();
    when 'M':  /* RI: reverse index - backwards LF */
      if (curs->y == screen->marg_t)
        term_do_scroll(screen->marg_t, screen->marg_b, -1, true);
      else if (curs->y > 0)
        curs->y--;
      curs->wrapnext = false;
    when 'Z':  /* DECID: terminal type query */
      ldisc_send(primary_da, sizeof primary_da - 1, 0);
    when 'c':  /* RIS: restore power-on settings */
      term_reset();
      term_clear_scrollback();
      if (term.reset_132) {
        win_resize(term.rows, 80);
        term.reset_132 = 0;
      }
      ldisc_send(null, 0, 0);
    when 'H':  /* HTS: set a tab */
      term.tabs[curs->x] = true;
    when ANSI('8', '#'):    /* DECALN: fills screen with Es :-) */
      for (int i = 0; i < term.rows; i++) {
        termline *line = screen->lines[i];
        for (int j = 0; j < term.cols; j++) {
          line->chars[j] =
            (termchar){.cc_next = 0, .chr = 'E', .attr = ATTR_DEFAULT};
        }
        line->attr = LATTR_NORM;
      }
      term.disptop = 0;
    when ANSI('3', '#'):  /* DECDHL: 2*height, top */
      screen->lines[curs->y]->attr = LATTR_TOP;
    when ANSI('4', '#'):  /* DECDHL: 2*height, bottom */
      screen->lines[curs->y]->attr = LATTR_BOT;
    when ANSI('5', '#'):  /* DECSWL: normal */
      screen->lines[curs->y]->attr = LATTR_NORM;
    when ANSI('6', '#'):  /* DECDWL: 2*width */
      screen->lines[curs->y]->attr = LATTR_WIDE;
    when ANSI('A', '(') or ANSI('B', '(') or ANSI('0', '('):
     /* GZD4: G0 designate 94-set */
      curs->csets[0] = c;
      term_update_cs();
    when ANSI('U', '('):  /* G0: OEM character set */
      curs->csets[0] = CSET_OEM;
      term_update_cs();
    when ANSI('A', ')') or ANSI('B', ')') or ANSI('0', ')'):
     /* G1D4: G1-designate 94-set */
      curs->csets[1] = c;
      term_update_cs();
    when ANSI('U', ')'): /* G1: OEM character set */
      curs->csets[1] = CSET_OEM;
      term_update_cs();
    when ANSI('8', '%') or ANSI('G', '%'):
      curs->utf = true;
      term_update_cs();
    when ANSI('@', '%'):
      curs->utf = false;
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
    term_cursor *curs = &term.screen.curs;
    switch (term.esc_args[i]) {
      when 0:  /* restore defaults */
        curs->attr = ATTR_DEFAULT;
      when 1:  /* enable bold */
        curs->attr |= ATTR_BOLD;
      when 2:  /* enable dim */
        curs->attr |= ATTR_DIM;
      when 4 or 21:  /* enable underline */
        curs->attr |= ATTR_UNDER;
      when 5:  /* enable blink */
        curs->attr |= ATTR_BLINK;
      when 7:  /* enable reverse video */
        curs->attr |= ATTR_REVERSE;
      when 8:  /* enable invisible text */
        curs->attr |= ATTR_INVISIBLE;
      when 10 ... 12: /* OEM acs off */
        curs->oem_acs = term.esc_args[i] - 10;
        term_update_cs();
      when 22: /* disable bold */
        curs->attr &= ~(ATTR_BOLD | ATTR_DIM);
      when 24: /* disable underline */
        curs->attr &= ~ATTR_UNDER;
      when 25: /* disable blink */
        curs->attr &= ~ATTR_BLINK;
      when 27: /* disable reverse video */
        curs->attr &= ~ATTR_REVERSE;
      when 28: /* disable invisible text */
        curs->attr &= ~ATTR_INVISIBLE;
      when 30 ... 37:
       /* foreground */
        curs->attr &= ~ATTR_FGMASK;
        curs->attr |=
          (term.esc_args[i] - 30) << ATTR_FGSHIFT;
      when 90 ... 97:
       /* aixterm-style bright foreground */
        curs->attr &= ~ATTR_FGMASK;
        curs->attr |= ((term.esc_args[i] - 90 + 8)
                           << ATTR_FGSHIFT);
      when 39: /* default-foreground */
        curs->attr &= ~ATTR_FGMASK;
        curs->attr |= ATTR_DEFFG;
      when 40 ... 47:
       /* background */
        curs->attr &= ~ATTR_BGMASK;
        curs->attr |=
          (term.esc_args[i] - 40) << ATTR_BGSHIFT;
      when 100 ... 107:
       /* aixterm-style bright background */
        curs->attr &= ~ATTR_BGMASK;
        curs->attr |= ((term.esc_args[i] - 100 + 8)
                           << ATTR_BGSHIFT);
      when 49: /* default-background */
        curs->attr &= ~ATTR_BGMASK;
        curs->attr |= ATTR_DEFBG;
      when 38: /* xterm 256-colour mode */
        if (i + 2 < nargs && term.esc_args[i + 1] == 5) {
          curs->attr &= ~ATTR_FGMASK;
          curs->attr |= ((term.esc_args[i + 2] & 0xFF)
                             << ATTR_FGSHIFT);
          i += 2;
        }
      when 48: /* xterm 256-colour mode */
        if (i + 2 < nargs && term.esc_args[i + 1] == 5) {
          curs->attr &= ~ATTR_BGMASK;
          curs->attr |= ((term.esc_args[i + 2] & 0xFF)
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
          if (term.deccolm_allowed) {
            term.selected = false;
            win_resize(term.rows, state ? 132 : 80);
            term.reset_132 = state;
            term.other_screen.marg_t = term.screen.marg_t = 0;
            term.other_screen.marg_b = term.screen.marg_b = term.rows - 1;
            move(0, 0, 0);
            term_erase_lots(false, true, true);
          }
        when 5:  /* DECSCNM: reverse video */
          if (state != term.rvideo) {
            term.rvideo = state;
            win_invalidate_all();
          }
        when 6:  /* DECOM: DEC origin mode */
          term.screen.dec_om = state;
        when 7:  /* DECAWM: auto wrap */
          term.screen.autowrap = state;
        when 8:  /* DECARM: auto key repeat */
          // ignore
        when 9:  /* X10_MOUSE */
          term.mouse_mode = state ? MM_X10 : 0;
          win_update_mouse();
        when 10: /* DECEDM: set local edit mode */
          term.editing = state;
          ldisc_send(null, 0, 0);
        when 25: /* DECTCEM: enable/disable cursor */
          term.cursor_on = state;
        when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
          term.deccolm_allowed = state;
        when 47: /* alternate screen */
          term.selected = false;
          term_switch_screen(state, false, false);
          term.disptop = 0;
        when 67: /* DECBKM: backarrow key mode */
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
          term.selected = false;
          term_switch_screen(state, true, true);
          term.disptop = 0;
        when 1048:       /* save/restore cursor */
          if (state)
            save_cursor();
          else
            restore_cursor();
        when 1049:       /* cursor & alternate screen */
          if (state)
            save_cursor();
          term.selected = false;
          term_switch_screen(state, true, false);
          if (!state)
            restore_cursor();
          term.disptop = 0;
        when 7700:       /* mintty only: CJK ambigous width reporting */
          term.report_ambig_width = state;
        when 7727:       /* mintty only: Application escape key mode */
          term.app_escape_key = state;
        when 7728:       /* mintty only: Escape sends FS (instead of ESC) */
          term.escape_sends_fs = state;
        when 7783:       /* mintty only: Shortcut override */
          term.shortcut_override = state;
        when 7787:       /* mintty only: Application mousewheel mode */
          term.app_wheel = state;
      }
    }
    else {
      switch (mode) {
        when 4:  /* IRM: set insert mode */
          term.screen.insert = state;
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
  term_screen *screen = &term.screen;
  term_cursor *curs = &screen->curs;
  int arg0 = term.esc_args[0], arg1 = term.esc_args[1];
  int def_arg0 = arg0 ?: 1;  // first arg with default
  int nargs = term.esc_nargs;
  switch (ANSI(c, term.esc_query)) {
    when 'A':        /* CUU: move up N lines */
      move(curs->x, curs->y - def_arg0, 1);
    when 'e':        /* VPR: move down N lines */
      move(curs->x, curs->y + def_arg0, 1);
    when 'B':        /* CUD: Cursor down */
      move(curs->x, curs->y + def_arg0, 1);
    when ANSI('c', '>'):     /* DA: report version */
      /* Terminal type 77 (ASCII 'M' for mintty) */
      if (!nargs || (nargs == 1 && arg0 == 0))
        ldisc_printf(0, "\e[>77;%u;0c", DECIMAL_VERSION);
    when 'a':        /* HPR: move right N cols */
      move(curs->x + def_arg0, curs->y, 1);
    when 'C':        /* CUF: Cursor right */
      move(curs->x + def_arg0, curs->y, 1);
    when 'D':        /* CUB: move left N cols */
      move(curs->x - def_arg0, curs->y, 1);
    when 'E':        /* CNL: move down N lines and CR */
      move(0, curs->y + def_arg0, 1);
    when 'F':        /* CPL: move up N lines and CR */
      move(0, curs->y - def_arg0, 1);
    when 'G' or '`':  /* CHA or HPA: set horizontal posn */
      move(def_arg0 - 1, curs->y, 0);
    when 'd':        /* VPA: set vertical posn */
      move(curs->x,
           ((screen->dec_om ? screen->marg_t : 0) +
            def_arg0 - 1), (screen->dec_om ? 2 : 0));
    when 'H' or 'f':  /* CUP or HVP: set horz and vert posns at once */
      if (nargs < 2)
        arg1 = ARG_DEFAULT;
      move((arg1 ?: 1) - 1,
           ((screen->dec_om ? screen->marg_t : 0) +
            def_arg0 - 1), (screen->dec_om ? 2 : 0));
    when 'J': {      /* ED: erase screen or parts of it */
      if (arg0 == 3) { /* Erase Saved Lines (xterm) */
        term_clear_scrollback();
        term.disptop = 0;
      }
      else {
        bool below = arg0 == 0 || arg0 == 2;
        bool above = arg0 == 1 || arg0 == 2;
        term_erase_lots(false, above, below);
      }
    }
    when 'K': {      /* EL: erase line or parts of it */
      bool right = arg0 == 0 || arg0 == 2;
      bool left  = arg0 == 1 || arg0 == 2;
      term_erase_lots(true, left, right);
    }
    when 'L':        /* IL: insert lines */
      if (curs->y <= screen->marg_b) {
        term_do_scroll(curs->y, screen->marg_b,
                       -def_arg0, false);
      }
    when 'M':        /* DL: delete lines */
      if (curs->y <= screen->marg_b) {
        term_do_scroll(curs->y, screen->marg_b,
                       def_arg0, true);
      }
    when '@':        /* ICH: insert chars */
      insert_char(def_arg0);
    when 'P':        /* DCH: delete chars */
      insert_char(-def_arg0);
    when 'c':        /* DA: terminal type query */
      ldisc_send(primary_da, sizeof primary_da - 1, 0);
    when 'n':        /* DSR: cursor position query */
      if (arg0 == 6)
        ldisc_printf(0, "\e[%d;%dR", curs->y + 1, curs->x + 1);
      else if (arg0 == 5) {
        ldisc_send("\e[0n", 4, 0);
      }
    when 'h' or ANSI_QUE('h'):  /* SM: toggle modes to high */
      set_modes(true);
    when 'l' or ANSI_QUE('l'):  /* RM: toggle modes to low */
      set_modes(false);
    when 'i' or ANSI_QUE('i'):  /* MC: Media copy */
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
      if (nargs == 1) {
        if (arg0 == 0) {
          term.tabs[curs->x] = false;
        }
        else if (arg0 == 3) {
          int i;
          for (i = 0; i < term.cols; i++)
            term.tabs[i] = false;
        }
      }
    when 'r':        /* DECSTBM: set scroll margins */
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
          screen->marg_t = top;
          screen->marg_b = bot;
          curs->x = 0;
         /*
          * I used to think the cursor should be
          * placed at the top of the newly marginned
          * area. Apparently not: VMS TPU falls over
          * if so.
          *
          * Well actually it should for
          * Origin mode - RDB
          */
          curs->y = (screen->dec_om ? screen->marg_t : 0);
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
        win_resize((arg0 ?: 24), term.cols);
        term.selected = false;
      }
      else if (nargs >= 1 && arg0 >= 1 && arg0 < 24) {
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
              win_maximise(arg1);
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
      term_do_scroll(screen->marg_t, screen->marg_b,
                     def_arg0, true);
      curs->wrapnext = false;
    when 'T':        /* SD: Scroll down */
      /* Avoid clash with hilight mouse tracking mode sequence */
      if (nargs <= 1) {
        term_do_scroll(screen->marg_t, screen->marg_b, -def_arg0, true);
        curs->wrapnext = false;
      }
    when ANSI('|', '*'):     /* DECSNLS */
     /* 
      * Set number of lines on screen
      * VT420 uses VGA like hardware and can
      * support any size in reasonable range
      * (24..49 AIUI) with no default specified.
      */
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
      if (nargs <= 1) {
        win_resize(term.rows, arg0 ?: cfg.cols);
        term.selected = false;
      }
    when 'X': {      /* ECH: write N spaces w/o moving cursor */
      int n = def_arg0;
      int p = curs->x;
      if (n > term.cols - curs->x)
        n = term.cols - curs->x;
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + n, curs->y);
      termline *line = screen->lines[curs->y];
      while (n--)
        line->chars[p++] = term.erase_char;
    }
    when 'x': {      /* DECREQTPARM: report terminal characteristics */
      if (arg0 <= 1)
        ldisc_printf(0, "\e[%c;1;1;112;112;1;0x", '2' + arg0);
    }
    when 'Z': {       /* CBT */
      int i = def_arg0; 
      while (--i >= 0 && curs->x > 0) {
        do {
          curs->x--;
        } while (curs->x > 0 && !term.tabs[curs->x]);
      }
    }
    when ANSI('m', '>'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (!nargs)
        term.modify_other_keys = 0;
      else if (arg0 == 4)
        term.modify_other_keys = nargs > 1 ? arg1 : 0;
    when ANSI('n', '>'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (nargs == 1 && arg0 == 4)
        term.modify_other_keys = 0;
    when ANSI('q', ' '):     /* DECSCUSR: set cursor style */
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
  if (!strcmp(s, "?")) {
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
  else if (sscanf(s, "%i,%i,%i%c", &r, &g, &b, &(char){0}) == 3)
    win_set_colour(i, make_colour(r, g, b));
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void
do_osc(void)
{
  if (!term.osc_w) { // "wordness" is ignored
    uint arg = term.esc_args[0];
    char *s = term.osc_string;
    s[term.osc_strlen] = 0;
    switch (arg) {
      when 0 or 2 or 21: win_set_title(s);  // ignore icon title
      when 4:  do_colour_osc(0);
      when 10: do_colour_osc(FG_COLOUR_I);
      when 11: do_colour_osc(BG_COLOUR_I);
      when 12: do_colour_osc(CURSOR_COLOUR_I);
      when 7770:
        if (!strcmp(s, "?"))
          ldisc_printf(0, "\e]7770;%u\e\\", win_get_font_size());
        else {
          char *end;
          int i = strtol(s, &end, 10);
          if (*end)
            ; // Ignore if parameter contains unexpected characters
          else if (*s == '+' || *s == '-')
            win_zoom_font(i);
          else
            win_set_font_size(i);
        }
      when 701 or 7776:  // Set/get locale. 701 is from urxvt.
        if (!strcmp(s, "?"))
          ldisc_printf(0, "\e]%u;%s\e\\", arg, cs_get_locale());
        else
          cs_set_locale(s);
    }
  }
}

void
term_write(const char *data, int len)
{
  bufchain_add(term.inbuf, data, len);

  if (term.in_term_write)
    return;
    
 /*
  * During drag-selects, we do not process terminal input,
  * because the user will want the screen to hold still to
  * be selected.
  */
  if (term_selecting())
    return;
    
  // Reset cursor blinking.
  term.cblinker = 1;
  term_schedule_cblink();

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

        if (term.screen.curs.oem_acs && !memchr("\e\n\r\b", c, 4)) {
          if (term.screen.curs.oem_acs == 2)
            c |= 0x80;
          write_char(cs_btowc_glyph(c), 1);
          continue;
        }
        
        switch (cs_mb1towc(&wc, c)) {
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
        
        switch(term.screen.curs.csets[term.screen.curs.g1]) {
          when CSET_LINEDRW:
            if (0x60 <= wc && wc < 0x80)
              wc = linedraw_chars[wc - 0x60];
          when CSET_GBCHR:
            if (c == '#')
              wc = 0xA3; // pound sign
          otherwise: ;
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
  win_schedule_update();
  term_print_flush();
}
