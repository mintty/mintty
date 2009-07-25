// term.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "linedisc.h"
#include "win.h"

struct term term;

/*
 * Call when the terminal's blinking-text settings change, or when
 * a text blink has just occurred.
 */
static void
tblink_cb(void)
{
  term.tblinker = !term.tblinker;
  term_schedule_tblink();
  win_update();
}

void
term_schedule_tblink(void)
{
  if (term.blink_is_real)
    win_set_timer(tblink_cb, 500);
  else
    term.tblinker = 1;  /* reset when not in use */
}

/*
 * Likewise with cursor blinks.
 */
static void
cblink_cb(void)
{
  term.cblinker = !term.cblinker;
  term_schedule_cblink();
  win_update();
}

void
term_schedule_cblink(void)
{
  if (term_cursor_blinks() && term.has_focus)
    win_set_timer(cblink_cb, cursor_blink_ticks());
  else
    term.cblinker = 1;  /* reset when not in use */
}

/*
 * Call to begin a visual bell.
 */
static void
vbell_cb(void)
{
  term.in_vbell = false;
  win_update();
}

void
term_schedule_vbell(int already_started, int startpoint)
{
  int ticks_gone = already_started ? get_tick_count() - startpoint : 0;
  int ticks = 100 - ticks_gone;
  if ((term.in_vbell = ticks > 0))
    win_set_timer(vbell_cb, ticks);
}

/* Find the bottom line on the screen that has any content.
 * If only the top line has content, returns 0.
 * If no lines have content, return -1.
 */
static int
find_last_nonempty_line(void)
{
  int i;
  for (i = count234(term.screen) - 1; i >= 0; i--) {
    termline *line = index234(term.screen, i);
    int j;
    for (j = 0; j < line->cols; j++)
      if (!termchars_equal(&line->chars[j], &term.erase_char))
        break;
    if (j != line->cols)
      break;
  }
  return i;
}

void
term_reset(void)
{
  term.alt_x = term.alt_y = 0;
  term.savecurs.x = term.savecurs.y = 0;
  term.alt_savecurs.x = term.alt_savecurs.y = 0;
  term.alt_t = term.marg_t = 0;
  if (term.rows != -1)
    term.alt_b = term.marg_b = term.rows - 1;
  else
    term.alt_b = term.marg_b = 0;
  if (term.cols != -1) {
    int i;
    for (i = 0; i < term.cols; i++)
      term.tabs[i] = (i % 8 == 0 ? true : false);
  }
  term.alt_om = term.dec_om = false;
  term.alt_ins = term.insert = false;
  term.alt_wnext = term.wrapnext = term.save_wnext = term.alt_save_wnext =
    false;
  term.alt_wrap = term.wrap = true;
  term.alt_cset = term.cset = term.save_cset = term.alt_save_cset = 0;
  term.alt_utf = term.utf = term.save_utf = term.alt_save_utf = 0;
  term.utf_state = 0;
  term.alt_oem_acs = term.oem_acs = term.save_oem_acs = term.alt_save_oem_acs =
    0;
  term.cset_attr[0] = term.cset_attr[1] = term.save_csattr =
    term.alt_save_csattr = CS_ASCII;
  term.rvideo = 0;
  term.in_vbell = false;
  term.cursor_on = true;
  term.default_attr = term.save_attr = term.alt_save_attr = term.curr_attr =
    ATTR_DEFAULT;
  term.editing = term.echoing = false;
  term.app_cursor_keys = false;
  term.use_bce = true;
  term.cursor_type = -1;
  term.cursor_blinks = -1;
  term.blink_is_real = cfg.allow_blinking;
  term.erase_char = term.basic_erase_char;
  term.which_screen = 0;
  term_print_finish();
  if (term.screen) {
    term_swap_screen(1, false, false);
    term_erase_lots(false, true, true);
    term_swap_screen(0, false, false);
    term_erase_lots(false, true, true);
    term.curs.y = find_last_nonempty_line() + 1;
    if (term.curs.y == term.rows) {
      term.curs.y--;
      term_do_scroll(0, term.rows - 1, 1, true);
    }
  }
  else {
    term.curs.y = 0;
  }
  term.curs.x = 0;
  term_schedule_tblink();
  term_schedule_cblink();
  term.disptop = 0;
}

/*
 * Called from front end when a keypress occurs, to trigger
 * anything magical that needs to happen in that situation.
 */
void
term_seen_key_event(void)
{
 /*
  * Reset the scrollback.
  */
  term.disptop = 0;   /* return to main screen */
  term.seen_disp_event = true;  /* for scrollback-reset-on-activity */
  win_schedule_update();
}

/*
 * When the user reconfigures us, we need to abandon a print job if
 * the user has disabled printing.
 */
void
term_reconfig(void)
{
  if (!*new_cfg.printer)
    term_print_finish();
  if (new_cfg.allow_blinking != cfg.allow_blinking)
    term.blink_is_real = new_cfg.allow_blinking;
  cfg.cursor_blinks = new_cfg.cursor_blinks;
  term_schedule_tblink();
  term_schedule_cblink();
  if (new_cfg.scrollback_lines != cfg.scrollback_lines) {
    cfg.scrollback_lines = new_cfg.scrollback_lines;
    term_resize(term.rows, term.cols);
  }
}

/*
 * Update the scroll bar.
 */
static void
update_sbar(void)
{
  int nscroll = sblines();
  win_set_sbar(nscroll + term.rows, nscroll + term.disptop, term.rows);
}

/*
 * Clear the scrollback.
 */
void
term_clear_scrollback(void)
{
  uchar *line;
  term.disptop = 0;
  while ((line = delpos234(term.scrollback, 0)))
    free(line); /* this is compressed data, not a termline */
  term.tempsblines = 0;
  update_sbar();
}

/*
 * Initialise the terminal.
 */
void
term_init(void)
{
 /*
  * Allocate a new Terminal structure and initialise the fields
  * that need it.
  */
  term.compatibility_level = TM_MINTTY;
  term.inbuf = new_bufchain();
  term.printer_buf = new_bufchain();
  term.state = TOPLEVEL;
  term.dispcursx = term.dispcursy = -1;
  term.rows = term.cols = -1;
  term_reset();
  term.attr_mask = 0xffffffff;

 /* FULL-TERMCHAR */
  term.basic_erase_char.chr = ' ';
  term.basic_erase_char.attr = ATTR_DEFAULT;
  term.erase_char = term.basic_erase_char;
}

/*
 * Set up the terminal for a given size.
 */
void
term_resize(int newrows, int newcols)
{
  tree234 *newalt;
  termline **newdisp, *line;
  int oldrows = term.rows;
  int save_which_screen = term.which_screen;

  if (newrows == term.rows && newcols == term.cols)
    return;     /* nothing to do */

 /* Behave sensibly if we're given zero (or negative) rows/cols */

  if (newrows < 1)
    newrows = 1;
  if (newcols < 1)
    newcols = 1;

  term.selected = false;
  term_swap_screen(0, false, false);

  term.alt_t = term.marg_t = 0;
  term.alt_b = term.marg_b = newrows - 1;

  if (term.rows == -1) {
    term.scrollback = newtree234(null);
    term.screen = newtree234(null);
    term.tempsblines = 0;
    term.rows = 0;
  }

 /*
  * Resize the screen and scrollback. We only need to shift
  * lines around within our data structures, because lineptr()
  * will take care of resizing each individual line if
  * necessary. So:
  * 
  *  - If the new screen is longer, we shunt lines in from temporary
  *    scrollback if possible, otherwise we add new blank lines at
  *    the bottom.
  *
  *  - If the new screen is shorter, we remove any blank lines at
  *    the bottom if possible, otherwise shunt lines above the cursor
  *    to scrollback if possible, otherwise delete lines below the
  *    cursor.
  * 
  *  - Then, if the new scrollback length is less than the
  *    amount of scrollback we actually have, we must throw some
  *    away.
  */
  int sblen = count234(term.scrollback);
 /* Do this loop to expand the screen if newrows > rows */
  assert(term.rows == count234(term.screen));
  while (term.rows < newrows) {
    if (term.tempsblines > 0) {
      uchar *cline;
     /* Insert a line from the scrollback at the top of the screen. */
      assert(sblen >= term.tempsblines);
      cline = delpos234(term.scrollback, --sblen);
      line = decompressline(cline, null);
      free(cline);
      line->temporary = false;  /* reconstituted line is now real */
      term.tempsblines -= 1;
      addpos234(term.screen, line, 0);
      term.curs.y += 1;
      term.savecurs.y += 1;
    }
    else {
     /* Add a new blank line at the bottom of the screen. */
      line = newline(newcols, false);
      addpos234(term.screen, line, count234(term.screen));
    }
    term.rows += 1;
  }
 /* Do this loop to shrink the screen if newrows < rows */
  while (term.rows > newrows) {
    if (term.curs.y < term.rows - 1) {
     /* delete bottom row, unless it contains the cursor */
      free(delpos234(term.screen, term.rows - 1));
    }
    else {
     /* push top row to scrollback */
      line = delpos234(term.screen, 0);
      addpos234(term.scrollback, compressline(line), sblen++);
      freeline(line);
      term.tempsblines += 1;
      term.curs.y -= 1;
      term.savecurs.y -= 1;
    }
    term.rows -= 1;
  }
  assert(term.rows == newrows);
  assert(count234(term.screen) == newrows);

 /* Delete any excess lines from the scrollback. */
  while (sblen > cfg.scrollback_lines) {
    line = delpos234(term.scrollback, 0);
    free(line);
    sblen--;
  }
  if (sblen < term.tempsblines)
    term.tempsblines = sblen;
  assert(count234(term.scrollback) <= cfg.scrollback_lines);
  assert(count234(term.scrollback) >= term.tempsblines);
  term.disptop = 0;

 /* Make a new displayed text buffer. */
  newdisp = newn(termline *, newrows);
  for (int i = 0; i < newrows; i++) {
    newdisp[i] = newline(newcols, false);
    for (int j = 0; j < newcols; j++)
      newdisp[i]->chars[j].attr = ATTR_INVALID;
  }
  if (term.disptext) {
    for (int i = 0; i < oldrows; i++)
      freeline(term.disptext[i]);
  }
  free(term.disptext);
  term.disptext = newdisp;
  term.dispcursx = term.dispcursy = -1;

 /* Make a new alternate screen. */
  newalt = newtree234(null);
  for (int i = 0; i < newrows; i++) {
    line = newline(newcols, true);
    addpos234(newalt, line, i);
  }
  if (term.alt_screen) {
    while (null != (line = delpos234(term.alt_screen, 0)))
      freeline(line);
    freetree234(term.alt_screen);
  }
  term.alt_screen = newalt;
  term.tabs = renewn(term.tabs, newcols);
  {
    int i;
    for (i = (term.cols > 0 ? term.cols : 0); i < newcols; i++)
      term.tabs[i] = (i % 8 == 0 ? true : false);
  }

 /* Check that the cursor positions are still valid. */
  if (term.savecurs.y < 0)
    term.savecurs.y = 0;
  if (term.savecurs.y >= newrows)
    term.savecurs.y = newrows - 1;
  if (term.curs.y < 0)
    term.curs.y = 0;
  if (term.curs.y >= newrows)
    term.curs.y = newrows - 1;
  if (term.curs.x >= newcols)
    term.curs.x = newcols - 1;
  term.alt_x = term.alt_y = 0;
  term.wrapnext = term.alt_wnext = false;

  term.rows = newrows;
  term.cols = newcols;

  term_swap_screen(save_which_screen, false, false);

  update_sbar();
}

/*
 * Swap screens. If `reset' is true and we have been asked to
 * switch to the alternate screen, we must bring most of its
 * configuration from the main screen and erase the contents of the
 * alternate screen completely. (This is even true if we're already
 * on it! Blame xterm.)
 */
void
term_swap_screen(int which, int reset, int keep_cur_pos)
{
  int t;
  pos tp;
  tree234 *ttr;

  if (!which)
    reset = false;      /* do no weird resetting if which==0 */

  if (which != term.which_screen) {
    term.which_screen = which;

    ttr = term.alt_screen;
    term.alt_screen = term.screen;
    term.screen = ttr;
    t = term.curs.x;
    if (!reset && !keep_cur_pos)
      term.curs.x = term.alt_x;
    term.alt_x = t;
    t = term.curs.y;
    if (!reset && !keep_cur_pos)
      term.curs.y = term.alt_y;
    term.alt_y = t;
    t = term.marg_t;
    if (!reset)
      term.marg_t = term.alt_t;
    term.alt_t = t;
    t = term.marg_b;
    if (!reset)
      term.marg_b = term.alt_b;
    term.alt_b = t;
    t = term.dec_om;
    if (!reset)
      term.dec_om = term.alt_om;
    term.alt_om = t;
    t = term.wrap;
    if (!reset)
      term.wrap = term.alt_wrap;
    term.alt_wrap = t;
    t = term.wrapnext;
    if (!reset)
      term.wrapnext = term.alt_wnext;
    term.alt_wnext = t;
    t = term.insert;
    if (!reset)
      term.insert = term.alt_ins;
    term.alt_ins = t;
    t = term.cset;
    if (!reset)
      term.cset = term.alt_cset;
    term.alt_cset = t;
    t = term.utf;
    if (!reset)
      term.utf = term.alt_utf;
    term.alt_utf = t;
    t = term.oem_acs;
    if (!reset)
      term.oem_acs = term.alt_oem_acs;
    term.alt_oem_acs = t;

    tp = term.savecurs;
    if (!reset && !keep_cur_pos)
      term.savecurs = term.alt_savecurs;
    term.alt_savecurs = tp;
    t = term.save_cset;
    if (!reset && !keep_cur_pos)
      term.save_cset = term.alt_save_cset;
    term.alt_save_cset = t;
    t = term.save_csattr;
    if (!reset && !keep_cur_pos)
      term.save_csattr = term.alt_save_csattr;
    term.alt_save_csattr = t;
    t = term.save_attr;
    if (!reset && !keep_cur_pos)
      term.save_attr = term.alt_save_attr;
    term.alt_save_attr = t;
    t = term.save_utf;
    if (!reset && !keep_cur_pos)
      term.save_utf = term.alt_save_utf;
    term.alt_save_utf = t;
    t = term.save_wnext;
    if (!reset && !keep_cur_pos)
      term.save_wnext = term.alt_save_wnext;
    term.alt_save_wnext = t;
    t = term.save_oem_acs;
    if (!reset && !keep_cur_pos)
      term.save_oem_acs = term.alt_save_oem_acs;
    term.alt_save_oem_acs = t;
  }

  if (reset && term.screen) {
   /*
    * Yes, this _is_ supposed to honour background-colour-erase.
    */
    term_erase_lots(false, true, true);
  }
}

/*
 * This function is called before doing _anything_ which affects
 * only part of a line of text. It is used to mark the boundary
 * between two character positions, and it indicates that some sort
 * of effect is going to happen on only one side of that boundary.
 * 
 * The effect of this function is to check whether a CJK
 * double-width character is straddling the boundary, and to remove
 * it and replace it with two spaces if so. (Of course, one or
 * other of those spaces is then likely to be replaced with
 * something else again, as a result of whatever happens next.)
 * 
 * Also, if the boundary is at the right-hand _edge_ of the screen,
 * it implies something deliberate is being done to the rightmost
 * column position; hence we must clear LATTR_WRAPPED2.
 * 
 * The input to the function is the coordinates of the _second_
 * character of the pair.
 */
void
term_check_boundary(int x, int y)
{
  termline *ldata;

 /* Validate input coordinates, just in case. */
  if (x == 0 || x > term.cols)
    return;

  ldata = scrlineptr(y);
  if (x == term.cols) {
    ldata->lattr &= ~LATTR_WRAPPED2;
  }
  else {
    if (ldata->chars[x].chr == UCSWIDE) {
      clear_cc(ldata, x - 1);
      clear_cc(ldata, x);
      ldata->chars[x - 1].chr = ' ';
      ldata->chars[x] = ldata->chars[x - 1];
    }
  }
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is true if the scrolling is permitted to
 * affect the scrollback buffer.
 */
void
term_do_scroll(int topline, int botline, int lines, int sb)
{
  termline *line;
  int i, seltop, olddisptop, shift;

  if (topline != 0 || term.which_screen != 0)
    sb = false;

  olddisptop = term.disptop;
  shift = lines;
  if (lines < 0) {
    while (lines < 0) {
      line = delpos234(term.screen, botline);
      resizeline(line, term.cols);
      for (i = 0; i < term.cols; i++)
        copy_termchar(line, i, &term.erase_char);
      line->lattr = LATTR_NORM;
      addpos234(term.screen, line, topline);

      if (term.sel_start.y >= topline && term.sel_start.y <= botline) {
        term.sel_start.y++;
        if (term.sel_start.y > botline) {
          term.sel_start.y = botline + 1;
          term.sel_start.x = 0;
        }
      }
      if (term.sel_end.y >= topline && term.sel_end.y <= botline) {
        term.sel_end.y++;
        if (term.sel_end.y > botline) {
          term.sel_end.y = botline + 1;
          term.sel_end.x = 0;
        }
      }

      lines++;
    }
  }
  else {
    while (lines > 0) {
      line = delpos234(term.screen, topline);
      if (sb && cfg.scrollback_lines > 0) {
        int sblen = count234(term.scrollback);
       /*
        * We must add this line to the scrollback. We'll
        * remove a line from the top of the scrollback if
        * the scrollback is full.
        */
        if (sblen == cfg.scrollback_lines) {
          sblen--;
          uchar *cline = delpos234(term.scrollback, 0);
          free(cline);
        }
        else
          term.tempsblines += 1;

        addpos234(term.scrollback, compressline(line), sblen);

       /* now `line' itself can be reused as the bottom line */

       /*
        * If the user is currently looking at part of the
        * scrollback, and they haven't enabled any options
        * that are going to reset the scrollback as a
        * result of this movement, then the chances are
        * they'd like to keep looking at the same line. So
        * we move their viewpoint at the same rate as the
        * scroll, at least until their viewpoint hits the
        * top end of the scrollback buffer, at which point
        * we don't have the choice any more.
        * 
        * Thanks to Jan Holmen Holsten for the idea and
        * initial implementation.
        */
        if (term.disptop > -cfg.scrollback_lines && term.disptop < 0)
          term.disptop--;
      }
      resizeline(line, term.cols);
      for (i = 0; i < term.cols; i++)
        copy_termchar(line, i, &term.erase_char);
      line->lattr = LATTR_NORM;
      addpos234(term.screen, line, botline);

     /*
      * If the selection endpoints move into the scrollback,
      * we keep them moving until they hit the top. However,
      * of course, if the line _hasn't_ moved into the
      * scrollback then we don't do this, and cut them off
      * at the top of the scroll region.
      * 
      * This applies to sel_start and sel_end (for an existing
      * selection), and also sel_anchor (for one being
      * selected as we speak).
      */
      seltop = sb ? -cfg.scrollback_lines : topline;

      if (term.selected) {
        if (term.sel_start.y >= seltop && term.sel_start.y <= botline) {
          term.sel_start.y--;
          if (term.sel_start.y < seltop) {
            term.sel_start.y = seltop;
            term.sel_start.x = 0;
          }
        }
        if (term.sel_end.y >= seltop && term.sel_end.y <= botline) {
          term.sel_end.y--;
          if (term.sel_end.y < seltop) {
            term.sel_end.y = seltop;
            term.sel_end.x = 0;
          }
        }
        if (term.sel_anchor.y >= seltop && term.sel_anchor.y <= botline) {
          term.sel_anchor.y--;
          if (term.sel_anchor.y < seltop) {
            term.sel_anchor.y = seltop;
            term.sel_anchor.x = 0;
          }
        }
      }

      lines--;
    }
  }
}


/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
void
term_erase_lots(int line_only, int from_begin, int to_end)
{
  pos start, end;
  int erase_lattr;
  int erasing_lines_from_top = 0;

  if (line_only) {
    start.y = term.curs.y;
    start.x = 0;
    end.y = term.curs.y + 1;
    end.x = 0;
    erase_lattr = false;
  }
  else {
    start.y = 0;
    start.x = 0;
    end.y = term.rows;
    end.x = 0;
    erase_lattr = true;
  }
  if (!from_begin) {
    start = term.curs;
  }
  if (!to_end) {
    end = term.curs;
    incpos(end);
  }
  if (!from_begin || !to_end)
    term_check_boundary(term.curs.x, term.curs.y);

 /* Clear screen also forces a full window redraw, just in case. */
  if (start.y == 0 && start.x == 0 && end.y == term.rows)
    win_invalidate_all();

 /* Lines scrolled away shouldn't be brought back on if the terminal
  * resizes. */
  if (start.y == 0 && start.x == 0 && end.x == 0 && erase_lattr)
    erasing_lines_from_top = 1;

  if (erasing_lines_from_top) {
   /* If it's a whole number of lines, starting at the top, and
    * we're fully erasing them, erase by scrolling and keep the
    * lines in the scrollback. */
    int scrolllines = end.y;
    if (end.y == term.rows) {
     /* Shrink until we find a non-empty row. */
      scrolllines = find_last_nonempty_line() + 1;
    }
    if (scrolllines > 0)
      term_do_scroll(0, scrolllines - 1, scrolllines, true);
  }
  else {
    termline *ldata = scrlineptr(start.y);
    while (poslt(start, end)) {
      if (start.x == term.cols) {
        if (!erase_lattr)
          ldata->lattr &= ~(LATTR_WRAPPED | LATTR_WRAPPED2);
        else
          ldata->lattr = LATTR_NORM;
      }
      else {
        copy_termchar(ldata, start.x, &term.erase_char);
      }
      if (incpos(start) && start.y < term.rows) {
        ldata = scrlineptr(start.y);
      }
    }
  }

 /* After an erase of lines from the top of the screen, we shouldn't
  * bring the lines back again if the terminal enlarges (since the user or
  * application has explictly thrown them away). */
  if (erasing_lines_from_top && !(term.which_screen))
    term.tempsblines = 0;
}


/*
 * ANSI printing routines.
 */
void
term_print_setup(void)
{
  bufchain_clear(term.printer_buf);
  term.print_job = printer_start_job(cfg.printer);
}

void
term_print_flush(void)
{
  void *data;
  int len;
  int size;
  while ((size = bufchain_size(term.printer_buf)) > 5) {
    bufchain_prefix(term.printer_buf, &data, &len);
    if (len > size - 5)
      len = size - 5;
    printer_job_data(term.print_job, data, len);
    bufchain_consume(term.printer_buf, len);
  }
}
void
term_print_finish(void)
{
  void *data;
  int len, size;
  char c;

  if (!term.printing && !term.only_printing)
    return;     /* we need do nothing */

  term_print_flush();
  while ((size = bufchain_size(term.printer_buf)) > 0) {
    bufchain_prefix(term.printer_buf, &data, &len);
    c = *(char *) data;
    if (c == '\033' || c == '\233') {
      bufchain_consume(term.printer_buf, size);
      break;
    }
    else {
      printer_job_data(term.print_job, &c, 1);
      bufchain_consume(term.printer_buf, 1);
    }
  }
  printer_finish_job(term.print_job);
  term.print_job = null;
  term.printing = term.only_printing = false;
}

void
term_paint(void)
{
  int chlen = 1024;
  wchar *ch = newn(wchar, chlen);
  termchar *newline = newn(termchar, term.cols);

 /* Depends on:
  * screen array, disptop, scrtop,
  * selection, cfg.blinkpc, blink_is_real, tblinker, 
  * curs.y, curs.x, cblinker, cfg.cursor_blinks, cursor_on, has_focus, wrapnext
  */

 /* Has the cursor position or type changed ? */
  int cursor;
  if (term.cursor_on) {
    if (term.has_focus) {
      if (term.cblinker || !term_cursor_blinks())
        cursor = TATTR_ACTCURS;
      else
        cursor = 0;
    }
    else
      cursor = TATTR_PASCURS;
    if (term.wrapnext)
      cursor |= TATTR_RIGHTCURS;
  }
  else
    cursor = 0;
 
  int our_curs_y = term.curs.y - term.disptop, our_curs_x = term.curs.x;
  {
   /*
    * Adjust the cursor position:
    *  - for bidi
    *  - in the case where it's resting on the right-hand half
    *    of a CJK wide character. xterm's behaviour here,
    *    which seems adequate to me, is to display the cursor
    *    covering the _whole_ character, exactly as if it were
    *    one space to the left.
    */
    termline *ldata = lineptr(term.curs.y);
    termchar *lchars = term_bidi_line(ldata, our_curs_y);

    if (lchars)
      our_curs_x = term.post_bidi_cache[our_curs_y].forward[our_curs_x];
    else
      lchars = ldata->chars;

    if (our_curs_x > 0 && lchars[our_curs_x].chr == UCSWIDE)
      our_curs_x--;

    unlineptr(ldata);
  }

 /*
  * If the cursor is not where it was last time we painted, and
  * its previous position is visible on screen, invalidate its
  * previous position.
  */
  if (term.dispcursy >= 0 &&
      (term.curstype != cursor || term.dispcursy != our_curs_y ||
       term.dispcursx != our_curs_x)) {
    termchar *dispcurs = term.disptext[term.dispcursy]->chars + term.dispcursx;

    if (term.dispcursx > 0 && dispcurs->chr == UCSWIDE)
      dispcurs[-1].attr |= ATTR_INVALID;
    if (term.dispcursx < term.cols - 1 && dispcurs[1].chr == UCSWIDE)
      dispcurs[1].attr |= ATTR_INVALID;
    dispcurs->attr |= ATTR_INVALID;

    term.curstype = 0;
  }
  term.dispcursx = term.dispcursy = -1;

 /* The normal screen data */
  for (int i = 0; i < term.rows; i++) {
    termline *ldata;
    termchar *lchars;
    int dirty_line, dirty_run;
    uint attr = 0;
    int updated_line = 0;
    int start = 0;
    int ccount = 0;
    int last_run_dirty = 0;
    int laststart, dirtyrect;
    int *backward;

    pos scrpos;
    scrpos.y = i + term.disptop;
    ldata = lineptr(scrpos.y);

   /* Do Arabic shaping and bidi. */
    lchars = term_bidi_line(ldata, i);
    if (lchars) {
      backward = term.post_bidi_cache[i].backward;
    }
    else {
      lchars = ldata->chars;
      backward = null;
    }

   /*
    * First loop: work along the line deciding what we want
    * each character cell to look like.
    */
    for (int j = 0; j < term.cols; j++) {
      uint tattr, tchar;
      termchar *d = lchars + j;
      scrpos.x = backward ? backward[j] : j;

      tchar = d->chr;
      tattr = d->attr;
      
      if (j < term.cols - 1 && d[1].chr == UCSWIDE)
        tattr |= ATTR_WIDE;

     /* Video reversing things */
      bool selected = 
        term.selected &&
        ( term.sel_rect
          ? posPle(term.sel_start, scrpos) && posPlt(scrpos, term.sel_end)
          : posle(term.sel_start, scrpos) && poslt(scrpos, term.sel_end)
        );
      if (term.in_vbell || selected)
        tattr ^= ATTR_REVERSE;

     /* 'Real' blinking ? */
      if (term.blink_is_real && (tattr & ATTR_BLINK)) {
        if (term.has_focus && term.tblinker) {
          tchar = ' ';
        }
        tattr &= ~ATTR_BLINK;
      }

     /*
      * Check the font we'll _probably_ be using to see if 
      * the character is wide when we don't want it to be.
      */
      if (tchar != term.disptext[i]->chars[j].chr ||
          tattr !=
          (term.disptext[i]->chars[j].attr & ~(ATTR_NARROW | DATTR_MASK))) {
        if ((tattr & ATTR_WIDE) == 0 && win_char_width(tchar) == 2)
          tattr |= ATTR_NARROW;
      }
      else if (term.disptext[i]->chars[j].attr & ATTR_NARROW)
        tattr |= ATTR_NARROW;

      if (i == our_curs_y && j == our_curs_x) {
        tattr |= cursor;
        term.curstype = cursor;
        term.dispcursx = j;
        term.dispcursy = i;
      }

     /* FULL-TERMCHAR */
      newline[j].attr = tattr;
      newline[j].chr = tchar;
     /* Combining characters are still read from lchars */
      newline[j].cc_next = 0;
    }

   /*
    * Now loop over the line again, noting where things have
    * changed.
    * 
    * During this loop, we keep track of where we last saw
    * DATTR_STARTRUN. Any mismatch automatically invalidates
    * _all_ of the containing run that was last printed: that
    * is, any rectangle that was drawn in one go in the
    * previous update should be either left completely alone
    * or overwritten in its entirety. This, along with the
    * expectation that front ends clip all text runs to their
    * bounding rectangle, should solve any possible problems
    * with fonts that overflow their character cells.
    */
    laststart = 0;
    dirtyrect = false;
    for (int j = 0; j < term.cols; j++) {
      if (term.disptext[i]->chars[j].attr & DATTR_STARTRUN) {
        laststart = j;
        dirtyrect = false;
      }

      if (term.disptext[i]->chars[j].chr != newline[j].chr ||
          (term.disptext[i]->chars[j].attr & ~DATTR_MASK)
          != newline[j].attr) {
        int k;

        if (!dirtyrect) {
          for (k = laststart; k < j; k++)
            term.disptext[i]->chars[k].attr |= ATTR_INVALID;

          dirtyrect = true;
        }
      }

      if (dirtyrect)
        term.disptext[i]->chars[j].attr |= ATTR_INVALID;
    }

   /*
    * Finally, loop once more and actually do the drawing.
    */
    dirty_run = dirty_line = (ldata->lattr != term.disptext[i]->lattr);
    term.disptext[i]->lattr = ldata->lattr;

    for (int j = 0; j < term.cols; j++) {
      uint tattr, tchar;
      int break_run, do_copy;
      termchar *d = lchars + j;

      tattr = newline[j].attr;
      tchar = newline[j].chr;

      if ((term.disptext[i]->chars[j].attr ^ tattr) & ATTR_WIDE)
        dirty_line = true;

      break_run = ((tattr ^ attr) & term.attr_mask) != 0;

     /* Special hack for VT100 Linedraw glyphs */
      if (tchar >= 0x23BA && tchar <= 0x23BD)
        break_run = true;

     /*
      * Break on both sides of any combined-character cell.
      */
      if (d->cc_next != 0 || (j > 0 && d[-1].cc_next != 0))
        break_run = true;

      if (!dirty_line) {
        if (term.disptext[i]->chars[j].chr == tchar &&
            (term.disptext[i]->chars[j].attr & ~DATTR_MASK) == tattr)
          break_run = true;
        else if (!dirty_run && ccount == 1)
          break_run = true;
      }

      if (break_run) {
        if ((dirty_run || last_run_dirty) && ccount > 0) {
          win_text(start, i, ch, ccount, attr, ldata->lattr);
          if (attr & (TATTR_ACTCURS | TATTR_PASCURS))
            win_cursor(start, i, ch, ccount, attr, ldata->lattr);

          updated_line = 1;
        }
        start = j;
        ccount = 0;
        attr = tattr;
        dirty_run = dirty_line;
      }

      do_copy = false;
      if (!termchars_equal_override
          (&term.disptext[i]->chars[j], d, tchar, tattr)) {
        do_copy = true;
        dirty_run = true;
      }

      if (ccount >= chlen) {
        chlen = ccount + 256;
        ch = renewn(ch, chlen);
      }
      ch[ccount++] = (wchar) tchar;

      if (d->cc_next) {
        termchar *dd = d;

        while (dd->cc_next) {
          uint schar;

          dd += dd->cc_next;

          schar = dd->chr;
          if (ccount >= chlen) {
            chlen = ccount + 256;
            ch = renewn(ch, chlen);
          }
          ch[ccount++] = (wchar) schar;
        }

        attr |= TATTR_COMBINING;
      }

      if (do_copy) {
        copy_termchar(term.disptext[i], j, d);
        term.disptext[i]->chars[j].chr = tchar;
        term.disptext[i]->chars[j].attr = tattr;
        if (start == j)
          term.disptext[i]->chars[j].attr |= DATTR_STARTRUN;
      }

     /* If it's a wide char step along to the next one. */
      if (tattr & ATTR_WIDE) {
        if (++j < term.cols) {
          d++;
         /*
          * By construction above, the cursor should not
          * be on the right-hand half of this character.
          * Ever.
          */
          assert(!(i == our_curs_y && j == our_curs_x));
          if (!termchars_equal(&term.disptext[i]->chars[j], d))
            dirty_run = true;
          copy_termchar(term.disptext[i], j, d);
        }
      }
    }
    if (dirty_run && ccount > 0) {
      win_text(start, i, ch, ccount, attr, ldata->lattr);
      if (attr & (TATTR_ACTCURS | TATTR_PASCURS))
        win_cursor(start, i, ch, ccount, attr, ldata->lattr);

      updated_line = 1;
    }
    unlineptr(ldata);
  }
  free(newline);
  free(ch);
}

void
term_update(void)
{
  if (term.seen_disp_event)
    update_sbar();
  term_paint();
  win_set_sys_cursor(term.curs.x, term.curs.y - term.disptop);
}

/*
 * Paint the window in response to a WM_PAINT message.
 */
void
term_invalidate(int left, int top, int right, int bottom)
{
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  if (right >= term.cols)
    right = term.cols - 1;
  if (bottom >= term.rows)
    bottom = term.rows - 1;

  for (int i = top; i <= bottom && i < term.rows; i++) {
    if ((term.disptext[i]->lattr & LATTR_MODE) == LATTR_NORM)
      for (int j = left; j <= right && j < term.cols; j++)
        term.disptext[i]->chars[j].attr |= ATTR_INVALID;
    else
      for (int j = left / 2; j <= right / 2 + 1 && j < term.cols; j++)
        term.disptext[i]->chars[j].attr |= ATTR_INVALID;
  }
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 */
void
term_scroll(int rel, int where)
{
  int sbtop = -sblines();
  term.disptop = (rel < 0 ? 0 : rel > 0 ? sbtop : term.disptop) + where;
  if (term.disptop < sbtop)
    term.disptop = sbtop;
  if (term.disptop > 0)
    term.disptop = 0;
  update_sbar();
  win_update();
}

/*
 * Helper routine for term_copy(): growing buffer.
 */
typedef struct {
  int buflen;   /* amount of allocated space in textbuf/attrbuf */
  int bufpos;   /* amount of actual data */
  wchar *textbuf;       /* buffer for copied text */
  wchar *textptr;       /* = textbuf + bufpos (current insertion point) */
  int *attrbuf; /* buffer for copied attributes */
  int *attrptr; /* = attrbuf + bufpos */
} clip_workbuf;

static void
clip_addchar(clip_workbuf * b, wchar chr, int attr)
{
  if (b->bufpos >= b->buflen) {
    b->buflen += 128;
    b->textbuf = renewn(b->textbuf, b->buflen);
    b->textptr = b->textbuf + b->bufpos;
    b->attrbuf = renewn(b->attrbuf, b->buflen);
    b->attrptr = b->attrbuf + b->bufpos;
  }
  *b->textptr++ = chr;
  *b->attrptr++ = attr;
  b->bufpos++;
}

void
term_copy(void)
{
  if (!term.selected)
    return;
  
  pos start = term.sel_start, end = term.sel_end;
  
  clip_workbuf buf;
  int old_top_x;
  int attr;

  buf.buflen = 5120;
  buf.bufpos = 0;
  buf.textptr = buf.textbuf = newn(wchar, buf.buflen);
  buf.attrptr = buf.attrbuf = newn(int, buf.buflen);

  old_top_x = start.x;    /* needed for rect==1 */

  while (poslt(start, end)) {
    int nl = false;
    termline *ldata = lineptr(start.y);
    pos nlpos;

   /*
    * nlpos will point at the maximum position on this line we
    * should copy up to. So we start it at the end of the
    * line...
    */
    nlpos.y = start.y;
    nlpos.x = term.cols;

   /*
    * ... move it backwards if there's unused space at the end
    * of the line (and also set `nl' if this is the case,
    * because in normal selection mode this means we need a
    * newline at the end)...
    */
    if (!(ldata->lattr & LATTR_WRAPPED)) {
      while (nlpos.x && ldata->chars[nlpos.x - 1].chr == ' ' &&
             !ldata->chars[nlpos.x - 1].cc_next && poslt(start, nlpos))
        decpos(nlpos);
      if (poslt(nlpos, end))
        nl = true;
    }
    else if (ldata->lattr & LATTR_WRAPPED2) {
     /* Ignore the last char on the line in a WRAPPED2 line. */
      decpos(nlpos);
    }

   /*
    * ... and then clip it to the terminal x coordinate if
    * we're doing rectangular selection. (In this case we
    * still did the above, so that copying e.g. the right-hand
    * column from a table doesn't fill with spaces on the
    * right.)
    */
    if (term.sel_rect) {
      if (nlpos.x > end.x)
        nlpos.x = end.x;
      nl = (start.y < end.y);
    }

    while (poslt(start, end) && poslt(start, nlpos)) {
      wchar cbuf[16], *p;
      int x = start.x;

      if (ldata->chars[x].chr == UCSWIDE) {
        start.x++;
        continue;
      }

      while (1) {
        wchar c = ldata->chars[x].chr;
        attr = ldata->chars[x].attr;
        cbuf[0] = c;
        cbuf[1] = 0;

        for (p = cbuf; *p; p++)
          clip_addchar(&buf, *p, attr);

        if (ldata->chars[x].cc_next)
          x += ldata->chars[x].cc_next;
        else
          break;
      }
      start.x++;
    }
    if (nl) {
      for (size_t i = 0; i < lengthof(sel_nl); i++)
        clip_addchar(&buf, sel_nl[i], 0);
    }
    start.y++;
    start.x = term.sel_rect ? old_top_x : 0;

    unlineptr(ldata);
  }
  if (sel_nul_terminated)
    clip_addchar(&buf, 0, 0);

 /* Finally, transfer all that to the clipboard. */
  win_copy(buf.textbuf, buf.attrbuf, buf.bufpos);
  free(buf.textbuf);
  free(buf.attrbuf);
}

void
term_paste(wchar *data, uint len)
{
  wchar *p, *q;

  term_seen_key_event();      /* pasted data counts */

  if (term.paste_buffer)
    free(term.paste_buffer);
  term.paste_pos = term.paste_len = 0;
  term.paste_buffer = newn(wchar, len);

  p = q = data;
  while (p < data + len) {
    while (p < data + len &&
           !(p <= data + len - lengthof(sel_nl) &&
             !memcmp(p, sel_nl, sizeof (sel_nl))))
      p++;

    for (int i = 0; i < p - q; i++)
      term.paste_buffer[term.paste_len++] = q[i];

    if (p <= data + len - lengthof(sel_nl) &&
        !memcmp(p, sel_nl, sizeof sel_nl)) {
      term.paste_buffer[term.paste_len++] = '\015';
      p += lengthof(sel_nl);
    }
    q = p;
  }
  
 /* Assume a small paste will be OK in one go. */
  if (term.paste_len < 256) {
    luni_send(term.paste_buffer, term.paste_len, 0);
    if (term.paste_buffer)
      free(term.paste_buffer);
    term.paste_buffer = 0;
    term.paste_pos = term.paste_len = 0;
  }
}

void
term_cancel_paste(void)
{
  if (term.paste_len == 0)
    return;
  free(term.paste_buffer);
  term.paste_buffer = null;
  term.paste_len = 0;
}

void
term_send_paste(void)
{
  if (term.paste_len == 0)
    return;

  while (term.paste_pos < term.paste_len) {
    int n = 0;
    while (n + term.paste_pos < term.paste_len) {
      if (term.paste_buffer[term.paste_pos + n++] == '\015')
        break;
    }
    luni_send(term.paste_buffer + term.paste_pos, n, 0);
    term.paste_pos += n;

    if (term.paste_pos < term.paste_len)
      return;
  }
  free(term.paste_buffer);
  term.paste_buffer = null;
  term.paste_len = 0;
}

void
term_select_all(void)
{
  term.sel_start = (pos){-sblines(), 0};
  term.sel_end = (pos){find_last_nonempty_line(), term.cols};
  term.selected = true;
  if (cfg.copy_on_select)
    term_copy();
}

void
term_deselect(void)
{
  if (term.selected) {
    term.selected = false;
    win_update();
  }
}

void
term_set_focus(int has_focus)
{
  if (has_focus != term.has_focus) {
    term.has_focus = has_focus;
    term_schedule_cblink();
    if (term.report_focus)
      ldisc_send(has_focus ? "\e[I" : "\e[O", 3, 0);
  }
}

bool
term_in_utf(void)
{
  return term.utf || ucsdata.codepage == unicode_codepage;
}

int
term_cursor_type(void)
{
  return term.cursor_type == -1 ? cfg.cursor_type : term.cursor_type;
}

bool
term_cursor_blinks(void)
{
  return term.cursor_blinks == -1 ? cfg.cursor_blinks : term.cursor_blinks;
}
