// termout.c (part of mintty)
// Copyright 2008-12 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "appinfo.h"
#include "charset.h"
#include "child.h"
#include "print.h"

#include <sys/termios.h>

/* This combines two characters into one value, for the purpose of pairing
 * any modifier byte and the final byte in escape sequences.
 */
#define CPAIR(x, y) ((x) << 8 | (y))

static const char primary_da[] = "\e[?1;2c";

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void
move(int x, int y, int marg_clip)
{
  term_cursor *curs = &term.curs;
  if (x < 0)
    x = 0;
  if (x >= term.cols)
    x = term.cols - 1;
  if (marg_clip) {
    if ((curs->y >= term.marg_top || marg_clip == 2) && y < term.marg_top)
      y = term.marg_top;
    if ((curs->y <= term.marg_bot || marg_clip == 2) && y > term.marg_bot)
      y = term.marg_bot;
  }
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
  term.erase_char.attr = curs->attr & (ATTR_FGMASK | ATTR_BGMASK);
  
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
  term_cursor *curs = &term.curs;

  n = (n < 0 ? -n : n);
  if (n > term.cols - curs->x)
    n = term.cols - curs->x;
  m = term.cols - curs->x - n;
  term_check_boundary(curs->x, curs->y);
  if (dir < 0)
    term_check_boundary(curs->x + n, curs->y);
  line = term.lines[curs->y];
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
  term_cursor *curs = &term.curs;
  if (curs->x == 0 && (curs->y == 0 || !curs->autowrap))
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
  term_cursor *curs = &term.curs;

  do
    curs->x++;
  while (curs->x < term.cols - 1 && !term.tabs[curs->x]);
  
  if ((term.lines[curs->y]->attr & LATTR_MODE) != LATTR_NORM) {
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
  term.curs.x = 0;
  term.curs.wrapnext = false;
}

static void
write_linefeed(void)
{
  term_cursor *curs = &term.curs;
  if (curs->y == term.marg_bot)
    term_do_scroll(term.marg_top, term.marg_bot, 1, true);
  else if (curs->y < term.rows - 1)
    curs->y++;
  curs->wrapnext = false;
}

static void
write_char(wchar c, int width)
{
  if (!c)
    return;
  
  term_cursor *curs = &term.curs;
  termline *line = term.lines[curs->y];
  void put_char(wchar c)
  {
    clear_cc(line, curs->x);
    line->chars[curs->x].chr = c;
    line->chars[curs->x].attr = curs->attr;
  }  

  if (curs->wrapnext && curs->autowrap && width > 0) {
    line->attr |= LATTR_WRAPPED;
    if (curs->y == term.marg_bot)
      term_do_scroll(term.marg_top, term.marg_bot, 1, true);
    else if (curs->y < term.rows - 1)
      curs->y++;
    curs->x = 0;
    curs->wrapnext = false;
    line = term.lines[curs->y];
  }
  if (term.insert && width > 0)
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
        if (curs->y == term.marg_bot)
          term_do_scroll(term.marg_top, term.marg_bot, 1, true);
        else if (curs->y < term.rows - 1)
          curs->y++;
        curs->x = 0;
        line = term.lines[curs->y];
       /* Now we must term_check_boundary again, of course. */
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + 2, curs->y);
      }
      put_char(c);
      curs->x++;
      put_char(UCSWIDE);
    when 0:  // Combining character.
      if (curs->x > 0) {
       /* If we're in wrapnext state, the character
        * to combine with is _here_, not to our left. */
        int x = curs->x - !curs->wrapnext;
       /*
        * If the previous character is
        * UCSWIDE, back up another one.
        */
        if (line->chars[x].chr == UCSWIDE) {
          assert(x > 0);
          x--;
        }
       /* Try to precompose with the cell's base codepoint */
        wchar pc = win_combine_chars(line->chars[x].chr, c);
        if (pc)
          line->chars[x].chr = pc;
        else
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

/* Process control character, returning whether it has been recognised. */
static bool
do_ctrl(char c)
{
  switch (c) {
    when '\e':   /* ESC: Escape */
      term.state = ESCAPE;
      term.esc_mod = 0;
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
      child_write(cfg.answerback, strlen(cfg.answerback));
    when CTRL('N'):   /* LS1: Locking-shift one */
      term.curs.g1 = true;
      term_update_cs();
    when CTRL('O'):   /* LS0: Locking-shift zero */
      term.curs.g1 = false;
      term_update_cs();
    otherwise:
      return false;
  }
  return true;
}

static void
do_esc(uchar c)
{
  term_cursor *curs = &term.curs;
  term.state = NORMAL;
  switch (CPAIR(term.esc_mod, c)) {
    when '[':  /* CSI: control sequence introducer */
      term.state = CSI_ARGS;
      term.csi_argc = 1;
      memset(term.csi_argv, 0, sizeof(term.csi_argv));
      term.esc_mod = 0;
    when ']':  /* OSC: operating system command */
      term.state = OSC_START;
    when 'P':  /* DCS: device control string */
      term.state = CMD_STRING;
      term.cmd_num = -1;
      term.cmd_len = 0;
    when '^' or '_': /* PM: privacy message, APC: application program command */
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
      write_return();
      write_linefeed();
    when 'M':  /* RI: reverse index - backwards LF */
      if (curs->y == term.marg_top)
        term_do_scroll(term.marg_top, term.marg_bot, -1, true);
      else if (curs->y > 0)
        curs->y--;
      curs->wrapnext = false;
    when 'Z':  /* DECID: terminal type query */
      child_write(primary_da, sizeof primary_da - 1);
    when 'c':  /* RIS: restore power-on settings */
      term_reset();
      if (term.reset_132) {
        win_set_chars(term.rows, 80);
        term.reset_132 = 0;
      }
    when 'H':  /* HTS: set a tab */
      term.tabs[curs->x] = true;
    when CPAIR('#', '8'):    /* DECALN: fills screen with Es :-) */
      for (int i = 0; i < term.rows; i++) {
        termline *line = term.lines[i];
        for (int j = 0; j < term.cols; j++) {
          line->chars[j] =
            (termchar){.cc_next = 0, .chr = 'E', .attr = ATTR_DEFAULT};
        }
        line->attr = LATTR_NORM;
      }
      term.disptop = 0;
    when CPAIR('#', '3'):  /* DECDHL: 2*height, top */
      term.lines[curs->y]->attr = LATTR_TOP;
    when CPAIR('#', '4'):  /* DECDHL: 2*height, bottom */
      term.lines[curs->y]->attr = LATTR_BOT;
    when CPAIR('#', '5'):  /* DECSWL: normal */
      term.lines[curs->y]->attr = LATTR_NORM;
    when CPAIR('#', '6'):  /* DECDWL: 2*width */
      term.lines[curs->y]->attr = LATTR_WIDE;
    when CPAIR('(', 'A') or CPAIR('(', 'B') or CPAIR('(', '0'):
     /* GZD4: G0 designate 94-set */
      curs->csets[0] = c;
      term_update_cs();
    when CPAIR('(', 'U'):  /* G0: OEM character set */
      curs->csets[0] = CSET_OEM;
      term_update_cs();
    when CPAIR(')', 'A') or CPAIR(')', 'B') or CPAIR(')', '0'):
     /* G1D4: G1-designate 94-set */
      curs->csets[1] = c;
      term_update_cs();
    when CPAIR(')', 'U'): /* G1: OEM character set */
      curs->csets[1] = CSET_OEM;
      term_update_cs();
    when CPAIR('%', '8') or CPAIR('%', 'G'):
      curs->utf = true;
      term_update_cs();
    when CPAIR('%', '@'):
      curs->utf = false;
      term_update_cs();
  }
}

static void
do_sgr(void)
{
 /* Set Graphics Rendition. */
  uint argc = term.csi_argc;
  uint attr = term.curs.attr;
  for (uint i = 0; i < argc; i++) {
    switch (term.csi_argv[i]) {
      when 0: attr = ATTR_DEFAULT | (attr & ATTR_PROTECTED);
      when 1: attr |= ATTR_BOLD;
      when 2: attr |= ATTR_DIM;
      when 4: attr |= ATTR_UNDER;
      when 5: attr |= ATTR_BLINK;
      when 7: attr |= ATTR_REVERSE;
      when 8: attr |= ATTR_INVISIBLE;
      when 10 ... 12:
        term.curs.oem_acs = term.csi_argv[i] - 10;
        term_update_cs();
      when 21: attr &= ~ATTR_BOLD;
      when 22: attr &= ~(ATTR_BOLD | ATTR_DIM);
      when 24: attr &= ~ATTR_UNDER;
      when 25: attr &= ~ATTR_BLINK;
      when 27: attr &= ~ATTR_REVERSE;
      when 28: attr &= ~ATTR_INVISIBLE;
      when 30 ... 37: /* foreground */
        attr &= ~ATTR_FGMASK;
        attr |= (term.csi_argv[i] - 30) << ATTR_FGSHIFT;
      when 90 ... 97: /* bright foreground */
        attr &= ~ATTR_FGMASK;
        attr |= ((term.csi_argv[i] - 90 + 8) << ATTR_FGSHIFT);
      when 38: /* 256-colour foreground */
        if (i + 2 < argc && term.csi_argv[i + 1] == 5) {
          attr &= ~ATTR_FGMASK;
          attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_FGSHIFT);
          i += 2;
        }
      when 39: /* default foreground */
        attr &= ~ATTR_FGMASK;
        attr |= ATTR_DEFFG;
      when 40 ... 47: /* background */
        attr &= ~ATTR_BGMASK;
        attr |= (term.csi_argv[i] - 40) << ATTR_BGSHIFT;
      when 100 ... 107: /* bright background */
        attr &= ~ATTR_BGMASK;
        attr |= ((term.csi_argv[i] - 100 + 8) << ATTR_BGSHIFT);
      when 48: /* 256-colour background */
        if (i + 2 < argc && term.csi_argv[i + 1] == 5) {
          attr &= ~ATTR_BGMASK;
          attr |= ((term.csi_argv[i + 2] & 0xFF) << ATTR_BGSHIFT);
          i += 2;
        }
      when 49: /* default background */
        attr &= ~ATTR_BGMASK;
        attr |= ATTR_DEFBG;
    }
  }
  term.curs.attr = attr;
  term.erase_char.attr = attr & (ATTR_FGMASK | ATTR_BGMASK);
}

/*
 * Set terminal modes in escape arguments to state.
 */
static void
set_modes(bool state)
{
  for (uint i = 0; i < term.csi_argc; i++) {
    int arg = term.csi_argv[i];
    if (term.esc_mod) {
      switch (arg) {
        when 1:  /* DECCKM: application cursor keys */
          term.app_cursor_keys = state;
        when 2:  /* DECANM: VT52 mode */
          // IGNORE
        when 3:  /* DECCOLM: 80/132 columns */
          if (term.deccolm_allowed) {
            term.selected = false;
            win_set_chars(term.rows, state ? 132 : 80);
            term.reset_132 = state;
            term.marg_top = 0;
            term.marg_bot = term.rows - 1;
            move(0, 0, 0);
            term_erase(false, false, true, true);
          }
        when 5:  /* DECSCNM: reverse video */
          if (state != term.rvideo) {
            term.rvideo = state;
            win_invalidate_all();
          }
        when 6:  /* DECOM: DEC origin mode */
          term.curs.origin = state;
        when 7:  /* DECAWM: auto wrap */
          term.curs.autowrap = state;
        when 8:  /* DECARM: auto key repeat */
          // ignore
        when 9:  /* X10_MOUSE */
          term.mouse_mode = state ? MM_X10 : 0;
          win_update_mouse();
        when 25: /* DECTCEM: enable/disable cursor */
          term.cursor_on = state;
        when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
          term.deccolm_allowed = state;
        when 47: /* alternate screen */
          term.selected = false;
          term_switch_screen(state, false);
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
        when 1005: /* Xterm's UTF8 encoding for mouse positions */
          term.mouse_enc = state ? ME_UTF8 : 0;
        when 1006: /* Xterm's CSI-style mouse encoding */
          term.mouse_enc = state ? ME_XTERM_CSI : 0;
        when 1015: /* Urxvt's CSI-style mouse encoding */
          term.mouse_enc = state ? ME_URXVT_CSI : 0;
        when 1047:       /* alternate screen */
          term.selected = false;
          term_switch_screen(state, true);
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
          term_switch_screen(state, true);
          if (!state)
            restore_cursor();
          term.disptop = 0;
        when 1061:       /* VT220 keyboard emulation */
          term.vt220_keys = state;
        when 2004:       /* xterm bracketed paste mode */
          term.bracketed_paste = state;

        /* Mintty private modes */
        when 7700:       /* CJK ambigous width reporting */
          term.report_ambig_width = state;
        when 7727:       /* Application escape key mode */
          term.app_escape_key = state;
        when 7728:       /* Escape sends FS (instead of ESC) */
          term.escape_sends_fs = state;
        when 7766:       /* Show/hide scrollbar (if enabled in config) */
          if (state != term.show_scrollbar) {
            term.show_scrollbar = state;
            if (cfg.scrollbar)
              win_update_scrollbar();
          }
        when 7783:       /* Shortcut override */
          term.shortcut_override = state;
        when 7786:       /* Mousewheel reporting */
          term.wheel_reporting = state;
        when 7787:       /* Application mousewheel mode */
          term.app_wheel = state;
      }
    }
    else {
      switch (arg) {
        when 4:  /* IRM: set insert mode */
          term.insert = state;
        when 12: /* SRM: set echo mode */
          term.echoing = !state;
        when 20: /* LNM: Return sends ... */
          term.newline_mode = state;
      }
    }
  }
}

/*
 * dtterm window operations and xterm extensions.
 */
static void
do_winop(void)
{
  int arg1 = term.csi_argv[1], arg2 = term.csi_argv[2];
  switch (term.csi_argv[0]) {
    when 1: win_set_iconic(false);
    when 2: win_set_iconic(true);
    when 3: win_set_pos(arg1, arg2);
    when 4: win_set_pixels(arg1, arg2);
    when 5: win_set_zorder(true);  // top
    when 6: win_set_zorder(false); // bottom
    when 7: win_invalidate_all();  // refresh
    when 8: win_set_chars(arg1 ?: cfg.rows, arg2 ?: cfg.cols);
    when 9: win_maximise(arg1);
    when 10: win_maximise(arg1 ? 2 : 0);  // fullscreen
    when 11: child_write(win_is_iconic() ? "\e[1t" : "\e[2t", 4);
    when 13: {
      int x, y;
      win_get_pos(&x, &y);
      child_printf("\e[3;%d;%dt", x, y);
    }
    when 14: {
      int height, width;
      win_get_pixels(&height, &width);
      child_printf("\e[4;%d;%dt", height, width);
    }
    when 18: child_printf("\e[8;%d;%dt", term.rows, term.cols);
    when 19: {
      int rows, cols;
      win_get_screen_chars(&rows, &cols);
      child_printf("\e[9;%d;%dt", rows, cols);
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
do_csi(uchar c)
{
  term_cursor *curs = &term.curs;
  int arg0 = term.csi_argv[0], arg1 = term.csi_argv[1];
  int arg0_def1 = arg0 ?: 1;  // first arg with default 1
  switch (CPAIR(term.esc_mod, c)) {
    when 'A':        /* CUU: move up N lines */
      move(curs->x, curs->y - arg0_def1, 1);
    when 'e':        /* VPR: move down N lines */
      move(curs->x, curs->y + arg0_def1, 1);
    when 'B':        /* CUD: Cursor down */
      move(curs->x, curs->y + arg0_def1, 1);
    when CPAIR('>', 'c'):     /* DA: report version */
      child_printf("\e[>77;%u;0c", DECIMAL_VERSION);
    when 'a':        /* HPR: move right N cols */
      move(curs->x + arg0_def1, curs->y, 1);
    when 'C':        /* CUF: Cursor right */
      move(curs->x + arg0_def1, curs->y, 1);
    when 'D':        /* CUB: move left N cols */
      move(curs->x - arg0_def1, curs->y, 1);
    when 'E':        /* CNL: move down N lines and CR */
      move(0, curs->y + arg0_def1, 1);
    when 'F':        /* CPL: move up N lines and CR */
      move(0, curs->y - arg0_def1, 1);
    when 'G' or '`':  /* CHA or HPA: set horizontal posn */
      move(arg0_def1 - 1, curs->y, 0);
    when 'd':        /* VPA: set vertical posn */
      move(curs->x,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'H' or 'f':  /* CUP or HVP: set horz and vert posns at once */
      move((arg1 ?: 1) - 1,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'J' or CPAIR('?', 'J'): { /* ED/DECSED: (selective) erase in display */
      if (arg0 == 3 && !term.esc_mod) { /* Erase Saved Lines (xterm) */
        term_clear_scrollback();
        term.disptop = 0;
      }
      else {
        bool above = arg0 == 1 || arg0 == 2;
        bool below = arg0 == 0 || arg0 == 2;
        term_erase(term.esc_mod, false, above, below);
      }
    }
    when 'K' or CPAIR('?', 'K'): { /* EL/DECSEL: (selective) erase in line */
      bool right = arg0 == 0 || arg0 == 2;
      bool left  = arg0 == 1 || arg0 == 2;
      term_erase(term.esc_mod, true, left, right);
    }
    when 'L':        /* IL: insert lines */
      if (curs->y >= term.marg_top && curs->y <= term.marg_bot)
        term_do_scroll(curs->y, term.marg_bot, -arg0_def1, false);
    when 'M':        /* DL: delete lines */
      if (curs->y >= term.marg_top && curs->y <= term.marg_bot)
        term_do_scroll(curs->y, term.marg_bot, arg0_def1, true);
    when '@':        /* ICH: insert chars */
      insert_char(arg0_def1);
    when 'P':        /* DCH: delete chars */
      insert_char(-arg0_def1);
    when 'c':        /* DA: terminal type query */
      child_write(primary_da, sizeof primary_da - 1);
    when 'n':        /* DSR: cursor position query */
      if (arg0 == 6)
        child_printf("\e[%d;%dR", curs->y + 1, curs->x + 1);
      else if (arg0 == 5)
        child_write("\e[0n", 4);
    when 'h' or CPAIR('?', 'h'):  /* SM: toggle modes to high */
      set_modes(true);
    when 'l' or CPAIR('?', 'l'):  /* RM: toggle modes to low */
      set_modes(false);
    when 'i' or CPAIR('?', 'i'):  /* MC: Media copy */
      if (arg0 == 5 && *cfg.printer) {
        term.printing = true;
        term.only_printing = !term.esc_mod;
        term.print_state = 0;
        printer_start_job(cfg.printer);
      }
      else if (arg0 == 4 && term.printing) {
        // Drop escape sequence from print buffer and finish printing.
        while (term.printbuf[--term.printbuf_pos] != '\e');
        term_print_finish();
      }
    when 'g':        /* TBC: clear tabs */
      if (!arg0)
        term.tabs[curs->x] = false;
      else if (arg0 == 3) {
        for (int i = 0; i < term.cols; i++)
          term.tabs[i] = false;
      }
    when 'r': {      /* DECSTBM: set scroll margins */
      int top = arg0_def1 - 1;
      int bot = (arg1 ? min(arg1, term.rows) : term.rows) - 1;
      if (bot > top) {
        term.marg_top = top;
        term.marg_bot = bot;
        curs->x = 0;
        curs->y = curs->origin ? term.marg_top : 0;
      }
    }
    when 'm':        /* SGR: set graphics rendition */
      do_sgr();
    when 's':        /* save cursor */
      save_cursor();
    when 'u':        /* restore cursor */
      restore_cursor();
    when 't':        /* DECSLPP: set page size - ie window height */
     /*
      * VT340/VT420 sequence DECSLPP, for setting the height of the window.
      * DEC only allowed values 24/25/36/48/72/144, so dtterm and xterm
      * claimed values below 24 for various window operations, and also
      * allowed any number of rows from 24 and above to be set.
      */
      if (arg0 >= 24) {
        win_set_chars(arg0, term.cols);
        term.selected = false;
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
      * VT420 uses VGA like hardware and can
      * support any size in reasonable range
      * (24..49 AIUI) with no default specified.
      */
      win_set_chars(arg0 ?: cfg.rows, term.cols);
      term.selected = false;
    when CPAIR('$', '|'):     /* DECSCPP */
     /*
      * Set number of columns per page
      * Docs imply range is only 80 or 132, but
      * I'll allow any.
      */
      win_set_chars(term.rows, arg0 ?: cfg.cols);
      term.selected = false;
    when 'X': {      /* ECH: write N spaces w/o moving cursor */
      int n = min(arg0_def1, term.cols - curs->x);
      int p = curs->x;
      term_check_boundary(curs->x, curs->y);
      term_check_boundary(curs->x + n, curs->y);
      termline *line = term.lines[curs->y];
      while (n--)
        line->chars[p++] = term.erase_char;
    }
    when 'x':        /* DECREQTPARM: report terminal characteristics */
      child_printf("\e[%c;1;1;112;112;1;0x", '2' + arg0);
    when 'Z': {      /* CBT (Cursor Backward Tabulation) */
      int n = arg0_def1; 
      while (--n >= 0 && curs->x > 0) {
        do
          curs->x--;
        while (curs->x > 0 && !term.tabs[curs->x]);
      }
    }
    when CPAIR('>', 'm'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (!arg0)
        term.modify_other_keys = 0;
      else if (arg0 == 4)
        term.modify_other_keys = arg1;
    when CPAIR('>', 'n'):     /* xterm: modifier key setting */
      /* only the modifyOtherKeys setting is implemented */
      if (arg0 == 4)
        term.modify_other_keys = 0;
    when CPAIR(' ', 'q'):     /* DECSCUSR: set cursor style */
      term.cursor_type = arg0 ? (arg0 - 1) / 2 : -1;
      term.cursor_blinks = arg0 ? arg0 % 2 : -1;
      term.cursor_invalid = true;
      term_schedule_cblink();
    when CPAIR('"', 'q'):  /* DECSCA: select character protection attribute */
      switch (arg0) {
        when 0 or 2: term.curs.attr &= ~ATTR_PROTECTED;
        when 1: term.curs.attr |= ATTR_PROTECTED;
      }
  }
}

static void
do_dcs(void)
{
  // Only DECRQSS (Request Status String) is implemented.
  // No DECUDK (User-Defined Keys) or xterm termcap/terminfo data.

  char *s = term.cmd_buf;

  if (*s++ != '$')
    return;
  
  uint attr = term.curs.attr;

  if (!strcmp(s, "qm")) { // SGR
    char buf[64], *p = buf;
    p += sprintf(p, "\eP1$r0");

    if (attr & ATTR_BOLD)
      p += sprintf(p, ";1");
    if (attr & ATTR_DIM)
      p += sprintf(p, ";2");
    if (attr & ATTR_UNDER)
      p += sprintf(p, ";4");
    if (attr & ATTR_BLINK)
      p += sprintf(p, ";5");
    if (attr & ATTR_REVERSE)
      p += sprintf(p, ";7");
    if (attr & ATTR_INVISIBLE)
      p += sprintf(p, ";8");

    if (term.curs.oem_acs)
      p += sprintf(p, ";%u", 10 + term.curs.oem_acs);

    uint fg = (attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
    if (fg != FG_COLOUR_I) {
      if (fg < 16)
        p += sprintf(p, ";%u", (fg < 8 ? 30 : 90) + (fg & 7));
      else
        p += sprintf(p, ";38;5;%u", fg);
    }

    uint bg = (attr & ATTR_BGMASK) >> ATTR_BGSHIFT;
    if (bg != BG_COLOUR_I) {
      if (bg < 16)
        p += sprintf(p, ";%u", (bg < 8 ? 40 : 100) + (bg & 7));
      else
        p += sprintf(p, ";48;5;%u", bg);
    }

    p += sprintf(p, "m\e\\");  // m for SGR, followed by ST

    child_write(buf, p - buf);
  }
  else if (!strcmp(s, "qr"))  // DECSTBM (scroll margins)
    child_printf("\eP1$r%u;%ur\e\\", term.marg_top + 1, term.marg_bot + 1);
  else if (!strcmp(s, "q\"p"))  // DECSCL (conformance level)
    child_write("\eP1$r61\"p\e\\", 11);  // report as VT100
  else if (!strcmp(s, "q\"q"))  // DECSCA (protection attribute)
    child_printf("\eP1$r%u\"q\e\\", (attr & ATTR_PROTECTED) != 0);
  else
    child_write((char[]){CTRL('X')}, 1);
}

static void
do_colour_osc(uint i)
{
  char *s = term.cmd_buf;
  bool has_index_arg = !i;
  if (has_index_arg) {
    int len = 0;
    sscanf(s, "%u;%n", &i, &len);
    if (!len || i >= COLOUR_NUM)
      return;
    s += len;
  }
  colour c;
  if (!strcmp(s, "?")) {
    child_printf("\e]%u;", term.cmd_num);
    if (has_index_arg)
      child_printf("%u;", i);
    c = win_get_colour(i);
    child_printf("rgb:%04x/%04x/%04x\e\\",
                 red(c) * 0x101, green(c) * 0x101, blue(c) * 0x101);
  }
  else if (parse_colour(s, &c))
    win_set_colour(i, c);
}

/*
 * Process OSC and DCS command sequences.
 */
static void
do_cmd(void)
{
  char *s = term.cmd_buf;
  s[term.cmd_len] = 0;
  switch (term.cmd_num) {
    when -1: do_dcs();
    when 0 or 2 or 21: win_set_title(s);  // ignore icon title
    when 4:  do_colour_osc(0);
    when 10: do_colour_osc(FG_COLOUR_I);
    when 11: do_colour_osc(BG_COLOUR_I);
    when 12: do_colour_osc(CURSOR_COLOUR_I);
    when 701:  // Set/get locale (from urxvt).
      if (!strcmp(s, "?"))
        child_printf("\e]701;%s\e\\", cs_get_locale());
      else
        cs_set_locale(s);
    when 7770:  // Change font size.
      if (!strcmp(s, "?"))
        child_printf("\e]7770;%u\e\\", win_get_font_size());
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
      win_check_glyphs(wcs, n);
      s = term.cmd_buf;
      for (size_t i = 0; i < n; i++) {
        *s++ = ';';
        if (wcs[i])
          s += sprintf(s, "%u", wcs[i]);
      }
      *s = 0;
      child_printf("\e]7771;!%s\e\\", term.cmd_buf);
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

/* Empty the input buffer */
void
term_flush(void)
{
  term_write(term.inbuf, term.inbuf_pos);
  free(term.inbuf);
  term.inbuf = 0;
  term.inbuf_pos = 0;
  term.inbuf_size = 0;
}

void
term_write(const char *buf, uint len)
{
 /*
  * During drag-selects, we do not process terminal input,
  * because the user will want the screen to hold still to
  * be selected.
  */
  if (term_selecting()) {
    if (term.inbuf_pos + len > term.inbuf_size) {
      term.inbuf_size = max(term.inbuf_pos, term.inbuf_size * 4 + 4096);
      term.inbuf = renewn(term.inbuf, term.inbuf_size);
    }
    memcpy(term.inbuf + term.inbuf_pos, buf, len);
    term.inbuf_pos += len;
    return;
  }
    
  // Reset cursor blinking.
  term.cblinker = 1;
  term_schedule_cblink();

  uint pos = 0;
  while (pos < len) {
    uchar c = buf[pos++];
    
   /*
    * If we're printing, add the character to the printer
    * buffer.
    */
    if (term.printing) {
      if (term.printbuf_pos >= term.printbuf_size) {
        term.printbuf_size = term.printbuf_size * 4 + 4096;
        term.printbuf = renewn(term.printbuf, term.printbuf_size);
      }
      term.printbuf[term.printbuf_pos++] = c;

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
          write_char(cs_btowc_glyph(c), 1);
          continue;
        }
        
        switch (cs_mb1towc(&wc, c)) {
          when 0: // NUL or low surrogate
            if (wc)
              pos--;
          when -1: // Encoding error
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
            int width = wcswidth((wchar[]){hwc, wc}, 2);
            #else
            int width = xcwidth(combine_surrogates(hwc, wc));
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
        
        if (is_high_surrogate(wc)) {
          term.high_surrogate = wc;
          continue;
        }
        
        // Control characters
        if (wc < 0x20 || wc == 0x7F) {
          if (!do_ctrl(wc) && c == wc) {
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
        
        switch(term.curs.csets[term.curs.g1]) {
          when CSET_LINEDRW:
            if (0x60 <= wc && wc <= 0x7E)
              wc = win_linedraw_chars[wc - 0x60];
          when CSET_GBCHR:
            if (c == '#')
              wc = 0xA3; // pound sign
          otherwise: ;
        }
        write_char(wc, width);
      }
      when ESCAPE or CMD_ESCAPE:
        if (c < 0x20)
          do_ctrl(c);
        else if (c < 0x30)
          term.esc_mod = term.esc_mod ? 0xFF : c;
        else if (c == '\\' && term.state == CMD_ESCAPE) {
          /* Process DCS or OSC sequence if we see ST. */
          do_cmd();
          term.state = NORMAL;
        }
        else
          do_esc(c);
      when CSI_ARGS:
        if (c < 0x20)
          do_ctrl(c);
        else if (c == ';') {
          if (term.csi_argc < lengthof(term.csi_argv))
            term.csi_argc++;
        }
        else if (c >= '0' && c <= '9') {
          uint i = term.csi_argc - 1;
          if (i < lengthof(term.csi_argv))
            term.csi_argv[i] = 10 * term.csi_argv[i] + c - '0';
        }
        else if (c < 0x40)
          term.esc_mod = term.esc_mod ? 0xFF : c;
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
          when '0' ... '9':  /* OSC command number */
            term.cmd_num = c - '0';
            term.state = OSC_NUM;
          when ';':
            term.cmd_num = 0;
            term.state = CMD_STRING;
          when '\a' or '\n' or '\r':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
          otherwise:
            term.state = IGNORE_STRING;
        }
      when OSC_NUM:
        switch (c) {
          when '0' ... '9':  /* OSC command number */
            term.cmd_num = term.cmd_num * 10 + c - '0';
          when ';':
            term.state = CMD_STRING;
          when '\a' or '\n' or '\r':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
          otherwise:
            term.state = IGNORE_STRING;
        }
      when OSC_PALETTE:
        if (isxdigit(c)) {
          // The dodgy Linux palette sequence: keep going until we have
          // seven hexadecimal digits.
          term.cmd_buf[term.cmd_len++] = c;
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
          when '\n' or '\r':
            term.state = NORMAL;
          when '\a':
            do_cmd();
            term.state = NORMAL;
          when '\e':
            term.state = CMD_ESCAPE;
          otherwise:
            if (term.cmd_len < lengthof(term.cmd_buf) - 1)
              term.cmd_buf[term.cmd_len++] = c;
        }
      when IGNORE_STRING:
        switch (c) {
          when '\n' or '\r' or '\a':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
        }
    }
  }
  win_schedule_update();
  if (term.printing) {
    printer_write(term.printbuf, term.printbuf_pos);
    term.printbuf_pos = 0;
  }
}
