// termout.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2017 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"
#include "winpriv.h"  // win_get_font, win_change_font

#include "win.h"
#include "appinfo.h"
#include "charset.h"
#include "child.h"
#include "print.h"
#include "sixel.h"
#include "winimg.h"
#include "base64.h"

#include <termios.h>

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
  term.erase_char.attr = curs->attr;
  term.erase_char.attr.attr &= (ATTR_FGMASK | ATTR_BGMASK);

 /* Make sure the window hasn't shrunk since the save */
  if (curs->x >= term.cols)
    curs->x = term.cols - 1;
  if (curs->y >= term.rows)
    curs->y = term.rows - 1;

 /*
  * wrapnext might reset to False
  * if the x position is no longer at the rightmost edge.
  */
  if (curs->wrapnext && curs->x < term.cols - 1)
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
  int dir = (n < 0 ? -1 : +1);
  int m;
  term_cursor *curs = &term.curs;
  termline *line = term.lines[curs->y];
  int cols = min(line->cols, line->size);

  n = (n < 0 ? -n : n);
  if (n > cols - curs->x)
    n = cols - curs->x;
  m = cols - curs->x - n;
  term_check_boundary(curs->x, curs->y);
  if (dir < 0)
    term_check_boundary(curs->x + n, curs->y);
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
  win_bell(&cfg);
}

static void
write_backspace(void)
{
  term_cursor *curs = &term.curs;
  int term_top = curs->origin ? term.marg_top : 0;
  if (curs->x == 0 && (curs->y == term_top || !curs->autowrap
                       || (!cfg.old_wrapmodes && !curs->rev_wrap)))
    /* skip */;
  else if (curs->x == 0 && curs->y > term_top)
    curs->x = term.cols - 1, curs->y--;
  else if (curs->wrapnext) {
    curs->wrapnext = false;
    if (!curs->rev_wrap && !cfg.old_wrapmodes)
      curs->x--;
  }
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
write_primary_da(void)
{
  string primary_da = primary_da3;
  char * vt = strstr(cfg.term, "vt");
  if (vt) {
    unsigned int ver;
    if (sscanf(vt + 2, "%u", &ver) == 1) {
      if (ver >= 300)
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
  if (!c)
    return;

  term_cursor *curs = &term.curs;
  termline *line = term.lines[curs->y];

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

  void put_char(wchar c)
  {
    clear_cc(line, curs->x);
    line->chars[curs->x].chr = c;
    line->chars[curs->x].attr = curs->attr;
  }

  if (curs->wrapnext && curs->autowrap && width > 0) {
    line->lattr |= LATTR_WRAPPED;
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
      if (curs->x == term.cols - 1) {
        line->chars[curs->x] = term.erase_char;
        line->lattr |= LATTR_WRAPPED | LATTR_WRAPPED2;
        if (curs->y == term.marg_bot)
          term_do_scroll(term.marg_top, term.marg_bot, 1, true);
        else if (curs->y < term.rows - 1)
          curs->y++;
        curs->x = 0;
        line = term.lines[curs->y];
       /* Now we must term_check_boundary again, of course. */
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + width, curs->y);
      }
      put_char(c);
      curs->x++;
#ifdef support_triple_width
      if (width > 2)
        curs->x += width - 2;
#endif
      put_char(UCSWIDE);
    when 0 or -1:  // Combining character or Low surrogate.
#ifdef debug_surrogates
      printf("write_char %04X %2d %08llX\n", c, width, curs->attr.attr);
#endif
      if (curs->x > 0) {
       /* If we're in wrapnext state, the character
        * to combine with is _here_, not to our left. */
        int x = curs->x - !curs->wrapnext;
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
          pc = win_combine_chars(line->chars[x].chr, c);
        else
          pc = 0;
        if (pc)
          line->chars[x].chr = pc;
        else
          add_cc(line, x, c, curs->attr);
      }
      return;
    otherwise:  // Anything else. Probably shouldn't get here.
      return;
  }

  curs->x++;
  if (curs->x == term.cols) {
    curs->x--;
    if (curs->autowrap || cfg.old_wrapmodes)
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
    when CTRL('E'): {  /* ENQ: terminal type query */
      //child_write(cfg.answerback, strlen(cfg.answerback));
      char * ab = cs__wcstombs(cfg.answerback);
      child_write(ab, strlen(ab));
      free(ab);
    }
    when CTRL('N'):   /* LS1: Locking-shift one */
      term.curs.gl = 1;
      term_update_cs();
    when CTRL('O'):   /* LS0: Locking-shift zero */
      term.curs.gl = 0;
      term_update_cs();
    otherwise:
      return false;
  }
  return true;
}

// compatible state machine expansion for NCR and DECRQM
static uchar esc_mod0 = 0;
static uchar esc_mod1 = 0;

static void
do_esc(uchar c)
{
  term_cursor *curs = &term.curs;
  term.state = NORMAL;

  // NRC tweaks
  uchar nrc_designate = 0;
  uchar nrc_select = 0;
  // first check for two-character character set designations (%5, %6)
  if (term.esc_mod == 0xFF && esc_mod1 == '%'
      && strchr("()-*.+/", esc_mod0)) {
    // transform two-character character set designations
    nrc_designate = esc_mod0;
    nrc_select = c == '5' ? CSET_DECSPGR : c;
  }
  // then check for further designations that work without decnrc_enabled
  else if (strchr("<", c) && strchr("()-*.+/", term.esc_mod)) {
    // '<': DEC Supplementary
    nrc_designate = term.esc_mod;
    nrc_select = c;
  }
  // ↕ this is a bit ugly (xterm uses a table), but hey it works
  if (term.curs.decnrc_enabled
     // also allow unguarded designations
     || (nrc_designate && strchr("%<", nrc_select))
     ) {
    if (!nrc_designate && strchr("()-*.+/", term.esc_mod)) {
      nrc_designate = term.esc_mod;
      // transform alternative designation indicators
      switch (c) {
        when 'C':  nrc_select = CSET_FI;
        when 'E':  nrc_select = CSET_NO;
        when '6':  nrc_select = CSET_NO;
        when 'H':  nrc_select = CSET_SE;
        when 'f':  nrc_select = CSET_FR;  // xterm, Kermit; not DEC
        when '9':  nrc_select = CSET_CA;  // xterm, DEC VT510; not VT320
        otherwise: nrc_select = c;
      }
    }
    // if a character set designation was identified, check if it's applicable
    if (nrc_designate) {
      if (strchr("<%45RQKY`6Z7=", nrc_select)) {
        // 94 character sets
        switch (nrc_designate) {
          when '(': curs->csets[0] = nrc_select;
          when ')': curs->csets[1] = nrc_select;
          when '*': curs->csets[2] = nrc_select;
          when '+': curs->csets[3] = nrc_select;
          otherwise: nrc_select = 0;
        }
      }
      else if (strchr("A", nrc_select)) {
        // 96 character sets
        switch (nrc_designate) {
          when '-': curs->csets[1] = nrc_select;
          when '.': curs->csets[2] = nrc_select;
          when '/': curs->csets[3] = nrc_select;
          otherwise: nrc_select = 0;
        }
      }
      else
        nrc_select = 0;
      // finish handling if a character set designation was applied
      if (nrc_select) {
        term_update_cs();
        return;
      }
    }
  }

  switch (CPAIR(term.esc_mod, c)) {
    when '[':  /* CSI: control sequence introducer */
      term.state = CSI_ARGS;
      term.csi_argc = 1;
      memset(term.csi_argv, 0, sizeof(term.csi_argv));
      memset(term.csi_argv_defined, 0, sizeof(term.csi_argv_defined));
      term.esc_mod = 0;
    when ']':  /* OSC: operating system command */
      term.state = OSC_START;
    when 'P':  /* DCS: device control string */
      term.state = DCS_START;
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
    when CPAIR('#', '8'):    /* DECALN: fills screen with Es :-) */
      for (int i = 0; i < term.rows; i++) {
        termline *line = term.lines[i];
        for (int j = 0; j < term.cols; j++) {
          line->chars[j] =
            (termchar) {.cc_next = 0, .chr = 'E', .attr = CATTR_DEFAULT};
        }
        line->lattr = LATTR_NORM;
      }
      term.disptop = 0;
    when CPAIR('#', '3'):  /* DECDHL: 2*height, top */
      term.lines[curs->y]->lattr = LATTR_TOP;
    when CPAIR('#', '4'):  /* DECDHL: 2*height, bottom */
      term.lines[curs->y]->lattr = LATTR_BOT;
    when CPAIR('#', '5'):  /* DECSWL: normal */
      term.lines[curs->y]->lattr = LATTR_NORM;
    when CPAIR('#', '6'):  /* DECDWL: 2*width */
      term.lines[curs->y]->lattr = LATTR_WIDE;
    when CPAIR('(', 'A') or CPAIR('(', 'B') or CPAIR('(', '0') or CPAIR('(', '>'):
     /* GZD4: G0 designate 94-set */
      curs->csets[0] = c;
      term_update_cs();
    when CPAIR('(', 'U'):  /* G0: OEM character set */
      curs->csets[0] = CSET_OEM;
      term_update_cs();
    when CPAIR(')', 'A') or CPAIR(')', 'B') or CPAIR(')', '0') or CPAIR(')', '>')
      or CPAIR('-', 'A') or CPAIR('-', 'B') or CPAIR('-', '0') or CPAIR('-', '>'):
     /* G1D4: G1-designate 94-set */
      curs->csets[1] = c;
      term_update_cs();
    when CPAIR(')', 'U'): /* G1: OEM character set */
      curs->csets[1] = CSET_OEM;
      term_update_cs();
    when CPAIR('*', 'A') or CPAIR('*', 'B') or CPAIR('*', '0') or CPAIR('*', '>')
      or CPAIR('.', 'A') or CPAIR('.', 'B') or CPAIR('.', '0') or CPAIR('.', '>'):
     /* Designate G2 character set */
      curs->csets[2] = c;
      term_update_cs();
    when CPAIR('+', 'A') or CPAIR('+', 'B') or CPAIR('+', '0') or CPAIR('+', '>')
      or CPAIR('/', 'A') or CPAIR('/', 'B') or CPAIR('/', '0') or CPAIR('/', '>'):
     /* Designate G3 character set */
      curs->csets[3] = c;
      term_update_cs();
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
  }
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
    switch (term.csi_argv[i]) {
      when 0:
        attr = CATTR_DEFAULT;
        attr.attr |= prot;
      when 1: attr.attr |= ATTR_BOLD;
      when 2: attr.attr |= ATTR_DIM;
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
      when 9: attr.attr |= ATTR_STRIKEOUT;
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
      when 22: attr.attr &= ~(ATTR_BOLD | ATTR_DIM);
      when 23:
        attr.attr &= ~ATTR_ITALIC;
        if (((attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT) + 10 == 20)
          attr.attr &= ~FONTFAM_MASK;
      when 24: attr.attr &= ~UNDER_MASK;
      when 25: attr.attr &= ~(ATTR_BLINK | ATTR_BLINK2);
      when 27: attr.attr &= ~ATTR_REVERSE;
      when 28: attr.attr &= ~ATTR_INVISIBLE;
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
}

/*
 * Set terminal modes in escape arguments to state.
 */
static void
set_modes(bool state)
{
  for (uint i = 0; i < term.csi_argc; i++) {
    int arg = term.csi_argv[i];
    if (term.esc_mod) { /* DECSET/DECRST: DEC private mode set/reset */
      switch (arg) {
        when 1:  /* DECCKM: application cursor keys */
          term.app_cursor_keys = state;
        when 2:  /* DECANM: VT100/VT52 mode */
          if (state) {
            // Designate USASCII for character sets G0-G3
            for (uint i = 0; i < lengthof(term.curs.csets); i++)
              term.curs.csets[i] = CSET_ASCII;
            term.curs.cset_single = CSET_ASCII;
            term_update_cs();
          }
          // IGNORE VT52
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
            win_invalidate_all(false);
          }
        when 6:  /* DECOM: DEC origin mode */
          term.curs.origin = state;
        when 7:  /* DECAWM: auto wrap */
          term.curs.autowrap = state;
          term.curs.wrapnext = false;
        when 45:  /* xterm: reverse (auto) wraparound */
          term.curs.rev_wrap = state;
          term.curs.wrapnext = false;
        when 8:  /* DECARM: auto key repeat */
          // ignore
        when 9:  /* X10_MOUSE */
          term.mouse_mode = state ? MM_X10 : 0;
          win_update_mouse();
        when 12: /* AT&T 610 blinking cursor */
          term.cursor_blinks = state;
          term.cursor_invalid = true;
          term_schedule_cblink();
        when 25: /* DECTCEM: enable/disable cursor */
          term.cursor_on = state;
          // Should we set term.cursor_invalid or call term_invalidate ?
        when 30: /* Show/hide scrollbar */
          if (state != term.show_scrollbar) {
            term.show_scrollbar = state;
            win_update_scrollbar(false);
          }
        when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
          term.deccolm_allowed = state;
        when 42: /* DECNRCM: national replacement character sets */
          term.curs.decnrc_enabled = state;
        when 67: /* DECBKM: backarrow key mode */
          term.backspace_sends_bs = state;
        when 80: /* DECSDM: SIXEL display mode */
          term.sixel_display = state;
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
      }
    }
    else { /* SM/RM: set/reset mode */
      switch (arg) {
        when 4:  /* IRM: set insert mode */
          term.insert = state;
        when 12: /* SRM: set echo mode */
          term.echoing = !state;
        when 20: /* LNM: Return sends ... */
          term.newline_mode = state;
#ifdef support_Wyse_cursor_modes
        when 33: /* WYSTCURM: steady Wyse cursor */
          term.cursor_blinks = !state;
          term.cursor_invalid = true;
          term_schedule_cblink();
        when 34: /* WYULCURM: Wyse underline cursor */
          term.cursor_type = state;
          term.cursor_blinks = false;
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
        return 2 - term.curs.autowrap;
      when 45:  /* xterm: reverse (auto) wraparound */
        return 2 - term.curs.rev_wrap;
      when 8:  /* DECARM: auto key repeat */
        return 3; // ignored
      when 9:  /* X10_MOUSE */
        return 2 - (term.mouse_mode == MM_X10);
      when 12: /* AT&T 610 blinking cursor */
        return 2 - term.cursor_blinks;
      when 25: /* DECTCEM: enable/disable cursor */
        return 2 - term.cursor_on;
      when 30: /* Show/hide scrollbar */
        return 2 - term.show_scrollbar;
      when 40: /* Allow/disallow DECCOLM (xterm c132 resource) */
        return 2 - term.deccolm_allowed;
      when 42: /* DECNRCM: national replacement character sets */
        return 2 - term.curs.decnrc_enabled;
      when 67: /* DECBKM: backarrow key mode */
        return 2 - term.backspace_sends_bs;
      when 80: /* DECSDM: SIXEL display mode */
        return 2 - term.sixel_display;
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
      otherwise:
        return 0;
    }
  }
  else { /* DECRQM for SM/RM: mode */
    switch (arg) {
      when 4:  /* IRM: insert mode */
        return 2 - term.insert;
      when 12: /* SRM: echo mode */
        return 2 - term.echoing;
      when 20: /* LNM: Return sends ... */
        return 2 - term.newline_mode;
#ifdef support_Wyse_cursor_modes
      when 33: /* WYSTCURM: steady Wyse cursor */
        return 2 - (!term.cursor_blinks);
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

/*
 * dtterm window operations and xterm extensions.
   CSI Ps ; Ps ; Ps t
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
      int rows0 = term.rows, cols0 = term.cols;
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
      }
      else if (arg1 == 0) {
        win_maximise(0);
        win_set_chars(term.rows0, term.cols0);
      }
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
    when 11: child_write(win_is_iconic() ? "\e[1t" : "\e[2t", 4);
    when 13: {
      int x, y;
      win_get_scrpos(&x, &y, arg1 == 2);
      child_printf("\e[3;%d;%dt", x, y);
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
      for (int i = 0; i < arg0_def1; i++) {
        if (h) {  // non-BMP
          write_char(h, last_width);
          write_char(c, -1);
        }
        else
          write_char(c, last_width);
      }
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
      if (!arg0)
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
    when 'G' or '`': /* CHA or HPA: set horizontal position */
      move(arg0_def1 - 1, curs->y, 0);
    when 'd':        /* VPA: set vertical position */
      move(curs->x,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'H' or 'f':  /* CUP or HVP: set horiz. and vert. positions at once */
      move((arg1 ?: 1) - 1,
           (curs->origin ? term.marg_top : 0) + arg0_def1 - 1,
           curs->origin ? 2 : 0);
    when 'I':  /* CHT: move right N TABs */
      for (int i = 0; i < arg0_def1; i++)
       write_tab();
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
    when CPAIR('#', '{'): { /* Push video attributes onto stack (XTPUSHSGR) */
      cattr ca = term.curs.attr;
      cattrflags caflagsmask = 0;

      void set_push(int attr) {
        switch (attr) {
          when 1: caflagsmask |= ATTR_BOLD;
          when 2: caflagsmask |= ATTR_DIM;
          when 3: caflagsmask |= ATTR_ITALIC;
          when 4 or 21: caflagsmask |= UNDER_MASK;
          when 5 or 6: caflagsmask |= ATTR_BLINK | ATTR_BLINK2;
          when 7: caflagsmask |= ATTR_REVERSE;
          when 8: caflagsmask |= ATTR_INVISIBLE;
          when 9: caflagsmask |= ATTR_STRIKEOUT;
          when 20: caflagsmask |= FONTFAM_MASK;
          when 53: caflagsmask |= ATTR_OVERL;
          when 58: caflagsmask |= ATTR_ULCOLOUR;
          when 10: caflagsmask |= ATTR_FGMASK;
          when 11: caflagsmask |= ATTR_BGMASK;
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
      if ((ca.attr & caflagsmask & ATTR_BGMASK) != TRUE_COLOUR)
        ca.truebg = 0;
      if (!(caflagsmask & ATTR_ULCOLOUR))
        ca.ulcolr = (colour)-1;
      // push
      //printf("XTPUSHSGR &%llX %llX %06X %06X %06X\n", caflagsmask, ca.attr, ca.truefg, ca.truebg, ca.ulcolr);
      push_attrs(ca, caflagsmask);
    }
    when CPAIR('#', '}'): { /* Pop video attributes from stack (XTPOPSGR) */
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
        if ((ca.attr & caflagsmask & ATTR_BGMASK) == TRUE_COLOUR)
          term.curs.attr.truebg = ca.truebg;
        if (caflagsmask & ATTR_ULCOLOUR)
          term.curs.attr.ulcolr = ca.ulcolr;
      }
    }
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
      * claimed values below 24 for various window operations,
      * and also allowed any number of rows from 24 and above to be set.
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
    when 'X': {      /* ECH: write N spaces w/o moving cursor */
      termline *line = term.lines[curs->y];
      int cols = min(line->cols, line->size);
      int n = min(arg0_def1, cols - curs->x);
      if (n > 0) {
        int p = curs->x;
        term_check_boundary(curs->x, curs->y);
        term_check_boundary(curs->x + n, curs->y);
        while (n--)
          line->chars[p++] = term.erase_char;
      }
    }
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
        when 0 or 2: term.curs.attr.attr &= ~ATTR_PROTECTED;
        when 1: term.curs.attr.attr |= ATTR_PROTECTED;
      }
    when 'n':        /* DSR: device status report */
      if (arg0 == 6)
        child_printf("\e[%d;%dR", curs->y + 1 - (curs->origin ? term.marg_top : 0), curs->x + 1);
      else if (arg0 == 5)
        child_write("\e[0n", 4);
    when CPAIR('?', 'n'):  /* DSR, DEC specific */
      switch (arg0) {
        when 6:
          child_printf("\e[?%d;%dR", curs->y + 1 - (curs->origin ? term.marg_top : 0), curs->x + 1);
        when 15:
          child_printf("\e[?%un", 11 - !!*cfg.printer);
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
  }
}

static void
do_dcs(void)
{
  // DECRQSS (Request Status String) and DECSIXEL are implemented.
  // No DECUDK (User-Defined Keys) or xterm termcap/terminfo data.

  char *s = term.cmd_buf;
  unsigned char *pixels;
  int i;
  imglist *cur, *img;
  colour bg, fg;
  cattr attr = term.curs.attr;
  int status = (-1);
  int x, y;
  int x0, y0;
  int attr0;
  int left, top, width, height, pixelwidth, pixelheight;
  sixel_state_t *st = 0;

  switch (term.dcs_cmd) {
  when 'q':

    st = (sixel_state_t *)term.imgs.parser_state;

// Revert https://github.com/mintty/mintty/commit/fe48cdc
// "fixed SIXEL colour registers handling"
// which led to Sixel display silently failing
// or even stalling mintty window (#740)
#define fixsix

#ifndef fixsix
#warning Sixel display bug #740 reenabled
#endif

    switch (term.state) {
    when DCS_PASSTHROUGH:
      if (!st)
        return;
#ifdef fixsix
      if (!st->image.data)
        return;
#endif
      status = sixel_parser_parse(st, (unsigned char *)s, term.cmd_len);
      if (status < 0) {
        sixel_parser_deinit(st);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        term.state = DCS_IGNORE;
        return;
      }

    when DCS_ESCAPE:
      if (!st)
        return;
#ifdef fixsix
      if (!st->image.data)
        return;
#endif
      status = sixel_parser_parse(st, (unsigned char *)s, term.cmd_len);
      if (status < 0) {
        sixel_parser_deinit(st);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }

#ifdef fixsix
      status = sixel_parser_finalize(st);
#else
      pixels = (unsigned char *)malloc(st->image.width * st->image.height * 4);
      if (!pixels)
        return;

      status = sixel_parser_finalize(st, pixels);
#endif
      if (status < 0) {
        sixel_parser_deinit(st);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }

#ifdef fixsix
      pixels = (unsigned char *)st->image.data;
      st->image.data = NULL;
#else
      sixel_parser_deinit(st);
#endif

      left = term.curs.x;
      top = term.virtuallines + (term.sixel_display ? 0: term.curs.y);
      width = st->image.width / st->grid_width;
      height = st->image.height / st->grid_height;
      pixelwidth = st->image.width;
      pixelheight = st->image.height;

      if (!winimg_new(&img, pixels, left, top, width, height, pixelwidth, pixelheight) != 0) {
        sixel_parser_deinit(st);
        free(term.imgs.parser_state);
        term.imgs.parser_state = NULL;
        return;
      }

      x0 = term.curs.x;
      attr0 = term.curs.attr.attr;

      // fill with space characters
      if (term.sixel_display) {  // sixel display mode
        y0 = term.curs.y;
        term.curs.y = 0;
        for (y = 0; y < img->height && y < term.rows; ++y) {
          term.curs.y = y;
          term.curs.x = 0;
          for (x = x0; x < x0 + img->width && x < term.cols; ++x)
            write_char(SIXELCH, 1);
        }
        term.curs.y = y0;
        term.curs.x = x0;
      } else {  // sixel scrolling mode
        for (i = 0; i < img->height; ++i) {
          term.curs.x = x0;
          for (x = x0; x < x0 + img->width && x < term.cols; ++x)
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

      if (term.imgs.first == NULL) {
        term.imgs.first = term.imgs.last = img;
      } else {
        for (cur = term.imgs.first; cur; cur = cur->next) {
          if (cur->pixelwidth == cur->width * st->grid_width &&
              cur->pixelheight == cur->height * st->grid_height)
          {
            if (img->top == cur->top && img->left == cur->left &&
                img->width == cur->width &&
                img->height == cur->height)
            {
              memcpy(cur->pixels, img->pixels, img->pixelwidth * img->pixelheight * 4);
              winimg_destroy(img);
              return;
            }
            if (img->top >= cur->top && img->left >= cur->left &&
                img->left + img->width <= cur->left + cur->width &&
                img->top + img->height <= cur->top + cur->height)
            {
              for (y = 0; y < img->pixelheight; ++y)
                memcpy(cur->pixels +
                         ((img->top - cur->top) * st->grid_height + y) * cur->pixelwidth * 4 +
                         (img->left - cur->left) * st->grid_width * 4,
                       img->pixels + y * img->pixelwidth * 4,
                       img->pixelwidth * 4);
              winimg_destroy(img);
              return;
            }
          }
        }
        term.imgs.last->next = img;
        term.imgs.last = img;
      }

    otherwise:
      /* parser status initialization */
      fg = win_get_colour(FG_COLOUR_I);
      bg = win_get_colour(BG_COLOUR_I);
      if (!st) {
        st = term.imgs.parser_state = calloc(1, sizeof(sixel_state_t));
        sixel_parser_set_default_color(st);
      }
#ifdef fixsix
      status = sixel_parser_init(st,
                                 (fg & 0xff) << 16 | (fg & 0xff00) | (fg & 0xff0000) >> 16,
                                 (bg & 0xff) << 16 | (bg & 0xff00) | (bg & 0xff0000) >> 16,
                                 term.private_color_registers);
#else
      status = sixel_parser_init(st, fg, bg, term.private_color_registers);
#endif
      if (status < 0)
        return;
    }

  when CPAIR('$', 'q'):
    switch (term.state) {
    when DCS_ESCAPE:
      if (!strcmp(s, "m")) { // SGR
        char buf[76], *p = buf;
        p += sprintf(p, "\eP1$r0");

        if (attr.attr & ATTR_BOLD)
          p += sprintf(p, ";1");
        if (attr.attr & ATTR_DIM)
          p += sprintf(p, ";2");
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
        if (attr.attr & ATTR_STRIKEOUT)
          p += sprintf(p, ";9");
        if ((attr.attr & UNDER_MASK) == ATTR_DOUBLYUND)
          p += sprintf(p, ";21");
        if (attr.attr & ATTR_FRAMED)
          p += sprintf(p, ";51;52");
        if (attr.attr & ATTR_OVERL)
          p += sprintf(p, ";53");

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
      } else if (!strcmp(s, "r")) {  // DECSTBM (scroll margins)
        child_printf("\eP1$r%u;%ur\e\\", term.marg_top + 1, term.marg_bot + 1);
      } else if (!strcmp(s, "\"p")) {  // DECSCL (conformance level)
        child_printf("\eP1$r%u;%u\"p\e\\", 63, 1);  // report as VT300
      } else if (!strcmp(s, "\"q")) {  // DECSCA (protection attribute)
        child_printf("\eP1$r%u\"q\e\\", (attr.attr & ATTR_PROTECTED) != 0);
      } else if (!strcmp(s, "s")) {  // DECSLRM (left and right margins)
        child_printf("\eP1$r%u;%us\e\\", 1, term.cols);
      } else if (!strcmp(s, " q")) {  // DECSCUSR (cursor style)
        child_printf("\eP1$r%u q\e\\",
                     (term.cursor_type >= 0 ? term.cursor_type * 2 : 0) + 1
                     + !(term.cursor_blinks & 1));
      } else if (!strcmp(s, "t") && term.rows >= 24) {  // DECSLPP (lines)
        child_printf("\eP1$r%ut\e\\", term.rows);
      } else if (!strcmp(s, "$|")) {  // DECSCPP (columns)
        child_printf("\eP1$r%u$|\e\\", term.cols);
      } else {
        child_printf("\eP0$r%s\e\\", s);
      }
    otherwise:
      return;
    }
  }
}

static void
do_colour_osc(bool has_index_arg, uint i, bool reset)
{
  char *s = term.cmd_buf;
  if (has_index_arg) {
    int osc = i;
    int len = 0;
    sscanf(s, "%u;%n", &i, &len);
    if ((reset ? len != 0 : len == 0) || i >= COLOUR_NUM)
      return;
    s += len;
    if (osc % 100 == 5) {
      if (i == 0)
        i = BOLD_COLOUR_I;
#ifdef other_color_substitutes
      else if (i == 1)
        i = UNDERLINE_COLOUR_I;
      else if (i == 2)
        i = BLINK_COLOUR_I;
      else if (i == 3)
        i = REVERSE_COLOUR_I;
      else if (i == 4)
        i = ITALIC_COLOUR_I;
#endif
      else
        return;
    }
    else if (i >= 256)
      return;
  }

  colour c;
  if (reset)
    win_set_colour(i, (colour)-1);
  else if (!strcmp(s, "?")) {
    child_printf("\e]%u;", term.cmd_num);
    if (has_index_arg)
      child_printf("%u;", i);
    c = i < COLOUR_NUM ? colours[i] : 0;  // should not be affected by rvideo
    child_printf("rgb:%04x/%04x/%04x\e\\",
                 red(c) * 0x101, green(c) * 0x101, blue(c) * 0x101);
  }
  else if (parse_colour(s, &c))
    win_set_colour(i, c);
}

/*
 * OSC52: \e]52;[cp0-6];?|base64-string\07"
 * Only system clipboard is supported now.
 */
static void do_clipboard(void)
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

  switch (term.cmd_num) {
    when 0 or 2: win_set_title(s);  // ignore icon title
    when 4:   do_colour_osc(true, 4, false);
    when 5:   do_colour_osc(true, 5, false);
    when 6 or 106: {
      int col, on;
      if (sscanf(term.cmd_buf, "%u;%u", &col, &on) == 2)
        if (col == 0)
          term.enable_bold_colour = on;
    }
    when 104: do_colour_osc(true, 4, true);
    when 105: do_colour_osc(true, 5, true);
    when 10:  do_colour_osc(false, FG_COLOUR_I, false);
    when 11:  if (*term.cmd_buf == '*') {
                wchar * bn = cs__mbstowcs(term.cmd_buf);
                wstrset(&cfg.background, bn);
                free(bn);
                win_invalidate_all(true);
              }
              else if (*term.cmd_buf == '_') {
                wchar * bn = cs__mbstowcs(term.cmd_buf + 1);
                wstrset(&cfg.background, bn);
                free(bn);
                win_invalidate_all(true);
              }
              else
                do_colour_osc(false, BG_COLOUR_I, false);
    when 12:  do_colour_osc(false, CURSOR_COLOUR_I, false);
    when 17:  do_colour_osc(false, SEL_COLOUR_I, false);
    when 19:  do_colour_osc(false, SEL_TEXT_COLOUR_I, false);
    when 110: do_colour_osc(false, FG_COLOUR_I, true);
    when 111: do_colour_osc(false, BG_COLOUR_I, true);
    when 112: do_colour_osc(false, CURSOR_COLOUR_I, true);
    when 117: do_colour_osc(false, SEL_COLOUR_I, true);
    when 119: do_colour_osc(false, SEL_TEXT_COLOUR_I, true);
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
        child_printf("\e]701;%s\e\\", cs_get_locale());
      else
        cs_set_locale(s);
    when 7721:  // Copy window title to clipboard.
        win_copy_title();
    when 7770:  // Change font size.
      if (!strcmp(s, "?"))
        child_printf("\e]7770;%u\e\\", win_get_font_size());
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
        child_printf("\e]7777;%u\e\\", win_get_font_size());
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
    when 50: {
      uint ff = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
      if (!strcmp(s, "?")) {
        char * fn = cs__wcstombs(win_get_font(ff) ?: W(""));
        child_printf("\e]50;%s\e\\", fn);
        free(fn);
      }
      else {
        if (ff < lengthof(cfg.fontfams) - 1) {
          wstring wfont = cs__mbstowcs(s);  // let this leak...
          win_change_font(ff, wfont);
        }
      }
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
  * because the user will want the screen to hold still to be selected.
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
          write_char(cs_btowc_glyph(c), 1);
          continue;
        }

        // handle NRC single shift and NRC GR invocation;
        // maybe we should handle control characters first?
        short cset = term.curs.csets[term.curs.gl];
        if (term.curs.cset_single != CSET_ASCII && c > 0x20 && c < 0xFF) {
          cset = term.curs.cset_single;
          term.curs.cset_single = CSET_ASCII;
        }
        else if (term.curs.decnrc_enabled
         && term.curs.gr && term.curs.csets[term.curs.gr] != CSET_ASCII
         && !term.curs.oem_acs && !term.curs.utf
         && c >= 0x80 && c < 0xFF) {
          // tune C1 behaviour to mimic xterm
          if (c < 0xA0)
            continue;
          // TODO: if we'd ever support 96 character sets (other than 'A')
          // 0xFF should be handled specifically

          c &= 0x7F;
          cset = term.curs.csets[term.curs.gr];
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
            int width = cfg.charwidth ? xcwidth(combine_surrogates(hwc, wc)) :
# ifdef __midipix__
                        wcwidth(combine_surrogates(hwc, wc));
# else
                        wcswidth((wchar[]){hwc, wc}, 2);
# endif
#else
            int width = xcwidth(combine_surrogates(hwc, wc));
#endif
            write_char(hwc, width);
            write_char(wc, -1);  // -1 indicates low surrogate
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

        cattrflags asav = term.curs.attr.attr;

        // Everything else
        int width;
        if (term.wide_indic && wc >= 0x0900 && indicwide(wc))
          width = 2;
        else if (term.wide_extra && wc >= 0x2000 && extrawide(wc)) {
          width = 2;
          if (win_char_width(wc) < 2)
            term.curs.attr.attr |= ATTR_EXPAND;
        }
        else
#if HAS_LOCALES
          if (cfg.charwidth)
            width = xcwidth(wc);
          else
            width = wcwidth(wc);
# ifdef hide_isolate_marks
          // force bidi isolate marks to be zero-width;
          // however, this is inconsistent with locale width
          if (wc >= 0x2066 && wc <= 0x2069)
            width = 0;  // bidi isolate marks
# endif
#else
          width = xcwidth(wc);
#endif

        if (width == 2
            // && wcschr(W("〈〉《》「」『』【】〒〓〔〕〖〗〘〙〚〛"), wc)
            && wc >= 0x3008 && wc <= 0x301B && (wc | 1) != 0x3013
            && win_char_width(wc) < 2
            // ensure symmetric handling of matching brackets
            && win_char_width(wc ^ 1) < 2)
        {
          term.curs.attr.attr |= ATTR_EXPAND;
        }

        wchar NRC(wchar * map)
        {
          static char * rpl = "#@[\\]^_`{|}~";
          char * match = strchr(rpl, c);
          if (match)
            return map[match - rpl];
          else
            return wc;
        }

        switch (cset) {
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
                term.curs.attr.attr |= ((cattrflags)dispcode) << ATTR_GRAPH_SHIFT;
              }
#endif
              wc = dispwc;
            }
          when CSET_TECH:  // DEC Technical character set
            if (c > ' ' && c < 0x7F) {
              // = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎛⎝⎞⎠⎨⎬␦␦╲╱␦␦␦␦␦␦␦≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ␦Σ␦␦√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ␦ν∂πψρστ␦ƒωξυζ←↑→↓")
              // = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎛⎝⎞⎠⎨⎬╶╶╲╱╴╴╳␦␦␦␦≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ␦Σ␦␦√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ␦ν∂πψρστ␦ƒωξυζ←↑→↓")
              wc = W("⎷┌─⌠⌡│⎡⎣⎤⎦⎧⎩⎫⎭⎨⎬╶╶╲╱╴╴╳␦␦␦␦≤≠≥∫∴∝∞÷Δ∇ΦΓ∼≃Θ×Λ⇔⇒≡ΠΨ␦Σ␦␦√ΩΞΥ⊂⊃∩∪∧∨¬αβχδεφγηιθκλ␦ν∂πψρστ␦ƒωξυζ←↑→↓")
                   [c - ' ' - 1];
              if (c <= 0x37) {
                static uchar techdraw_code[23] = {
                  0xE0,                    // square root base
                  0, 0, 0, 0, 0,
                  0xE8, 0xE9, 0xEA, 0xEB,  // square bracket corners
                  0, 0, 0, 0,              // curly bracket hooks
                  0, 0,                    // curly bracket middle pieces
                  0xE1, 0xE2, 0, 0, 0xE5, 0xE6, 0xE7  // sum segments
                };
                uchar dispcode = techdraw_code[c - 0x21];
                term.curs.attr.attr |= ((cattrflags)dispcode) << ATTR_GRAPH_SHIFT;
              }
            }
          when CSET_GBCHR:  // NRC United Kingdom
            if (c == '#')
              wc = 0xA3; // pound sign
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
              wc = W("¡¢£␦¥␦§¤©ª«␦␦␦␦°±²³␦µ¶·␦¹º»¼½␦¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ␦ÑÒÓÔÕÖŒØÙÚÛÜŸ␦ßàáâãäåæçèéêëìíîï␦ñòóôõöœøùúûüÿ␦")
                   [c - ' ' - 1];
            }
          otherwise: ;
        }

        if (wc >= 0x2580 && wc <= 0x259F) {
          // Block Elements (U+2580-U+259F)
          // ▀▁▂▃▄▅▆▇█▉▊▋▌▍▎▏▐░▒▓▔▕▖▗▘▙▚▛▜▝▞▟
          term.curs.attr.attr |= ((cattrflags)(wc & 0xFF)) << ATTR_GRAPH_SHIFT;
        }

        write_char(wc, width);
        term.curs.attr.attr = asav;
      } // end term_write switch (term.state) when NORMAL

      when ESCAPE or CMD_ESCAPE:
        if (c < 0x20)
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
            term.state = NORMAL;
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
          when '\n' or '\r':
            term.state = NORMAL;
          when '\a':
            do_cmd();
            term.state = NORMAL;
          when '\e':
            term.state = CMD_ESCAPE;
          otherwise:
            term_push_cmd(c);
        }

      when IGNORE_STRING:
        switch (c) {
          when '\n' or '\r' or '\a':
            term.state = NORMAL;
          when '\e':
            term.state = ESCAPE;
        }

      when DCS_START:
        term.cmd_num = -1;
        term.cmd_len = 0;
        term.dcs_cmd = 0;
        switch (c) {
          when '@' ... '~':  /* DCS cmd final byte */
            term.dcs_cmd = c;
            do_dcs();
            term.state = DCS_PASSTHROUGH;
          when '\e':
            term.state = DCS_ESCAPE;
          when '0' ... '9':  /* DCS parameter */
            term.state = DCS_PARAM;
          when ';':          /* DCS separator */
            term.state = DCS_PARAM;
          when ':':
            term.state = DCS_IGNORE;
          when '<' ... '?':
            term.dcs_cmd = c;
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
            do_dcs();
            term.state = DCS_PASSTHROUGH;
          when '\e':
            term.state = DCS_ESCAPE;
            term.esc_mod = 0;
          when '0' ... '9' or ';' or ':':  /* DCS parameter */
            term.state = DCS_PARAM;
          when '<' ... '?':
            term.dcs_cmd = term.dcs_cmd << 8 | c;
            term.state = DCS_PARAM;
          when ' ' ... '/':  /* DCS intermediate byte */
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
