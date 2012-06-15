// term.c (part of mintty)
// Copyright 2008-12 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "charset.h"
#include "child.h"

struct term term;

const termchar
basic_erase_char = { .cc_next = 0, .chr = ' ', .attr = ATTR_DEFAULT };

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
int
term_last_nonempty_line(void)
{
  for (int i = term.rows - 1; i >= 0; i--) {
    termline *line = term.lines[i];
    if (line) {
      for (int j = 0; j < line->cols; j++)
        if (!termchars_equal(&line->chars[j], &term.erase_char))
          return i;
    }
  }
  return -1;
}

static void
term_cursor_reset(term_cursor *curs)
{
  curs->attr = ATTR_DEFAULT;
  curs->csets[0] = curs->csets[1] = CSET_ASCII;
  curs->autowrap = true;
}

void
term_reset(void)
{
  term.state = NORMAL;

  term_cursor_reset(&term.curs);
  term_cursor_reset(&term.saved_cursors[0]);
  term_cursor_reset(&term.saved_cursors[1]);
  
  term.backspace_sends_bs = cfg.backspace_sends_bs;
  if (term.tabs) {
    for (int i = 0; i < term.cols; i++)
      term.tabs[i] = (i % 8 == 0);
  }
  term.rvideo = 0;
  term.in_vbell = false;
  term.cursor_on = true;
  term.echoing = false;
  term.insert = false;
  term.shortcut_override = term.escape_sends_fs = term.app_escape_key = false;
  term.vt220_keys = strstr(cfg.term, "vt220");
  term.app_keypad = term.app_cursor_keys = term.app_wheel = false;
  term.mouse_mode = MM_NONE;
  term.mouse_enc = ME_X10;
  term.wheel_reporting = true;
  term.modify_other_keys = 0;
  term.report_focus = term.report_ambig_width = 0;
  term.bracketed_paste = false;
  term.show_scrollbar = true;

  term.marg_top = 0;
  term.marg_bot = term.rows - 1;

  term.cursor_type = -1;
  term.cursor_blinks = -1;
  term.blink_is_real = cfg.allow_blinking;
  term.erase_char = basic_erase_char;
  term.on_alt_screen = false;
  term_print_finish();
  if (term.lines) {
    term_switch_screen(1, false);
    term_erase(false, false, true, true);
    term_switch_screen(0, false);
    term_erase(false, false, true, true);
    term.curs.y = term_last_nonempty_line() + 1;
    if (term.curs.y == term.rows) {
      term.curs.y--;
      term_do_scroll(0, term.rows - 1, 1, true);
    }
  }
  term.selected = false;
  term_schedule_tblink();
  term_schedule_cblink();
  term_clear_scrollback();
  
  win_reset_colours();
}

static void
show_screen(bool other_screen)
{
  term.show_other_screen = other_screen;
  term.disptop = 0;
  term.selected = false;

  // Reset cursor blinking.
  if (!other_screen) {
    term.cblinker = 1;
    term_schedule_cblink();
  }

  win_update();
}

/* Return to active screen and reset scrollback */
void
term_reset_screen(void)
{
  show_screen(false);
}

/* Switch display to other screen and reset scrollback */
void
term_flip_screen(void)
{
  show_screen(!term.show_other_screen);
}

/* Apply changed settings */
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
  if (new_cfg.backspace_sends_bs != cfg.backspace_sends_bs)
    term.backspace_sends_bs = new_cfg.backspace_sends_bs;
  if (strcmp(new_cfg.term, cfg.term))
    term.vt220_keys = strstr(new_cfg.term, "vt220");
}

static void
scrollback_push(uchar *line)
{
  if (term.sblines == term.sblen) {
    // Need to make space for the new line.
    if (term.sblen < cfg.scrollback_lines) {
      // Expand buffer
      assert(term.sbpos == 0);
      int new_sblen = min(cfg.scrollback_lines, term.sblen * 3 + 1024);
      term.scrollback = renewn(term.scrollback, new_sblen);
      term.sbpos = term.sblen;
      term.sblen = new_sblen;
    }
    else if (term.sblines) {
      // Throw away the oldest line
      free(term.scrollback[term.sbpos]);
      term.sblines--;
    }
    else
      return;
  }
  assert(term.sblines < term.sblen);
  assert(term.sbpos < term.sblen);
  term.scrollback[term.sbpos++] = line;
  if (term.sbpos == term.sblen)
    term.sbpos = 0;
  term.sblines++;
  if (term.tempsblines < term.sblines)
    term.tempsblines++;
}

static uchar *
scrollback_pop(void)
{
  assert(term.sblines > 0);
  assert(term.sbpos < term.sblen);
  term.sblines--;
  if (term.tempsblines)
    term.tempsblines--;
  if (term.sbpos == 0)
    term.sbpos = term.sblen;
  return term.scrollback[--term.sbpos];
}

/*
 * Clear the scrollback.
 */
void
term_clear_scrollback(void)
{
  while (term.sblines)
    free(scrollback_pop());
  free(term.scrollback);
  term.scrollback = 0;
  term.sblen = term.sblines = term.sbpos = 0;
  term.tempsblines = 0;
  term.disptop = 0;
}

/*
 * Set up the terminal for a given size.
 */
void
term_resize(int newrows, int newcols)
{
  bool on_alt_screen = term.on_alt_screen;
  term_switch_screen(0, false);

  term.selected = false;

  term.marg_top = 0;
  term.marg_bot = newrows - 1;

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

  termlines *lines = term.lines;
  term_cursor *curs = &term.curs;
  term_cursor *saved_curs = &term.saved_cursors[term.on_alt_screen];

  // Shrink the screen if newrows < rows
  if (newrows < term.rows) {
    int removed = term.rows - newrows;
    int destroy = min(removed, term.rows - (curs->y + 1));
    int store = removed - destroy;
    
    // Push removed lines into scrollback
    for (int i = 0; i < store; i++) {
      termline *line = lines[i];
      scrollback_push(compressline(line));
      freeline(line);
    }

    // Move up remaining lines
    memmove(lines, lines + store, newrows * sizeof(termline *));
    
    // Destroy removed lines below the cursor
    for (int i = term.rows - destroy; i < term.rows; i++)
      freeline(lines[i]);
    
    // Adjust cursor position
    curs->y = max(0, curs->y - store);
    saved_curs->y = max(0, saved_curs->y - store);
  }

  term.lines = lines = renewn(lines, newrows);
  
  // Expand the screen if newrows > rows
  if (newrows > term.rows) {
    int added = newrows - term.rows;
    int restore = min(added, term.tempsblines);
    int create = added - restore;
    
    // Fill bottom of screen with blank lines
    for (int i = newrows - create; i < newrows; i++)
      lines[i] = newline(newcols, false);
    
    // Move existing lines down
    memmove(lines + restore, lines, term.rows * sizeof(termline *));
    
    // Restore lines from scrollback
    for (int i = restore; i--;) {
      uchar *cline = scrollback_pop();
      termline *line = decompressline(cline, null);
      free(cline);
      line->temporary = false;  /* reconstituted line is now real */
      lines[i] = line;
    }
    
    // Adjust cursor position
    curs->y += restore;
    saved_curs->y += restore;
  }
  
  // Resize lines
  for (int i = 0; i < newrows; i++)
    resizeline(lines[i], newcols);
  
  // Make a new displayed text buffer.
  if (term.displines) {
    for (int i = 0; i < term.rows; i++)
      freeline(term.displines[i]);
  }
  term.displines = renewn(term.displines, newrows);
  for (int i = 0; i < newrows; i++) {
    termline *line = newline(newcols, false);
    term.displines[i] = line;
    for (int j = 0; j < newcols; j++)
      line->chars[j].attr = ATTR_INVALID;
  }

  // Make a new alternate screen.
  lines = term.other_lines;
  if (lines) {
    for (int i = 0; i < term.rows; i++)
      freeline(lines[i]);
  }
  term.other_lines = lines = renewn(lines, newrows);
  for (int i = 0; i < newrows; i++)
    lines[i] = newline(newcols, true);

  // Reset tab stops
  term.tabs = renewn(term.tabs, newcols);
  for (int i = (term.cols > 0 ? term.cols : 0); i < newcols; i++)
    term.tabs[i] = (i % 8 == 0);

  // Check that the cursor positions are still valid.
  assert(0 <= curs->y && curs->y < newrows);
  assert(0 <= saved_curs->y && saved_curs->y < newrows);
  curs->x = min(curs->x, newcols - 1);

  curs->wrapnext = false;

  term.disptop = 0;

  term.rows = newrows;
  term.cols = newcols;

  term_switch_screen(on_alt_screen, false);
}

/*
 * Swap screens. If `reset' is true and we have been asked to
 * switch to the alternate screen, we must bring most of its
 * configuration from the main screen and erase the contents of the
 * alternate screen completely.
 */
void
term_switch_screen(bool to_alt, bool reset)
{
  if (to_alt == term.on_alt_screen)
    return;

  term.on_alt_screen = to_alt;

  termlines *oldlines = term.lines;
  term.lines = term.other_lines;
  term.other_lines = oldlines;
  
  if (to_alt && reset)
    term_erase(false, false, true, true);
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
 /* Validate input coordinates, just in case. */
  if (x == 0 || x > term.cols)
    return;

  termline *line = term.lines[y];
  if (x == term.cols)
    line->attr &= ~LATTR_WRAPPED2;
  else if (line->chars[x].chr == UCSWIDE) {
    clear_cc(line, x - 1);
    clear_cc(line, x);
    line->chars[x - 1].chr = ' ';
    line->chars[x] = line->chars[x - 1];
  }
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is true if the scrolling is permitted to
 * affect the scrollback buffer.
 */
void
term_do_scroll(int topline, int botline, int lines, bool sb)
{
  assert(botline >= topline && lines != 0);
  
  bool down = lines < 0; // Scrolling downwards?
  lines = abs(lines);    // Number of lines to scroll by
  
  botline++; // One below the scroll region: easier to calculate with
  
  // Don't try to scroll more than the number of lines in the scroll region.
  int lines_in_region = botline - topline;
  lines = min(lines, lines_in_region);
  
  // Number of lines that are moved up or down as they are.
  // The rest are scrolled out of the region and replaced by empty lines.
  int moved_lines = lines_in_region - lines;
  
  // Useful pointers to the top and (one below the) bottom lines.
  termline **top = term.lines + topline;
  termline **bot = term.lines + botline;
  
  // Reuse lines that are being scrolled out of the scroll region,
  // clearing their content.
  termline *recycled[abs(lines)];
  void recycle(termline **src) {
    memcpy(recycled, src, sizeof recycled);
    for (int i = 0; i < lines; i++)
      clearline(recycled[i]);
  }

  if (down) {
    // Move down remaining lines and push in the recycled lines
    recycle(bot - lines);
    memmove(top + lines, top, moved_lines * sizeof(termline *));
    memcpy(top, recycled, sizeof recycled);

    // Move selection markers if they're within the scroll region
    void scroll_pos(pos *p) {
      if (!term.show_other_screen && p->y >= topline && p->y < botline) {
        if ((p->y += lines) >= botline)
          *p = (pos){.y = botline, .x = 0};
      }
    }
    scroll_pos(&term.sel_start);
    scroll_pos(&term.sel_anchor);
    scroll_pos(&term.sel_end);
  }
  else {
    int seltop = topline;

    // Only push lines into the scrollback when scrolling off the top of the
    // normal screen and scrollback is actually enabled.
    if (sb && topline == 0 && !term.on_alt_screen && cfg.scrollback_lines) {
      for (int i = 0; i < lines; i++)
        scrollback_push(compressline(term.lines[i]));
 
      // Shift viewpoint accordingly if user is looking at scrollback
      if (term.disptop < 0)
        term.disptop = max(term.disptop - lines, -term.sblines);

      seltop = -term.sblines;
    }
    
    // Move up remaining lines and push in the recycled lines
    recycle(top);
    memmove(top, top + lines, moved_lines * sizeof(termline *));
    memcpy(bot - lines, recycled, sizeof recycled);

    // Move selection markers if they're within the scroll region
    void scroll_pos(pos *p) {
      if (!term.show_other_screen && p->y >= seltop && p->y < botline) {
        if ((p->y -= lines) < seltop)
          *p = (pos){.y = seltop, .x = 0};
      }
    }
    scroll_pos(&term.sel_start);
    scroll_pos(&term.sel_anchor);
    scroll_pos(&term.sel_end);
  }
}


/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
void
term_erase(bool selective, bool line_only, bool from_begin, bool to_end)
{
  term_cursor *curs = &term.curs;
  pos start, end;

  if (from_begin)
    start = (pos){.y = line_only ? curs->y : 0, .x = 0};
  else
    start = (pos){.y = curs->y, .x = curs->x};

  if (to_end)
    end = (pos){.y = line_only ? curs->y + 1 : term.rows, .x = 0};
  else
    end = (pos){.y = curs->y, .x = curs->x}, incpos(end);
  
  if (!from_begin || !to_end)
    term_check_boundary(curs->x, curs->y);

 /* Lines scrolled away shouldn't be brought back on if the terminal resizes. */
  bool erasing_lines_from_top =
    start.y == 0 && start.x == 0 && end.x == 0 && !line_only && !selective;

  if (erasing_lines_from_top) {
   /* If it's a whole number of lines, starting at the top, and
    * we're fully erasing them, erase by scrolling and keep the
    * lines in the scrollback. */
    int scrolllines = end.y;
    if (end.y == term.rows) {
     /* Shrink until we find a non-empty row. */
      scrolllines = term_last_nonempty_line() + 1;
    }
    if (scrolllines > 0)
      term_do_scroll(0, scrolllines - 1, scrolllines, true);

   /* After an erase of lines from the top of the screen, we shouldn't
    * bring the lines back again if the terminal enlarges (since the user or
    * application has explictly thrown them away). */
    if (!term.on_alt_screen)
      term.tempsblines = 0;
  }
  else {
    termline *line = term.lines[start.y];
    while (poslt(start, end)) {
      if (start.x == term.cols) {
        if (line_only)
          line->attr &= ~(LATTR_WRAPPED | LATTR_WRAPPED2);
        else
          line->attr = LATTR_NORM;
      }
      else if (!selective || !(line->chars[start.x].attr & ATTR_PROTECTED))
        line->chars[start.x] = term.erase_char;
      if (incpos(start) && start.y < term.rows)
        line = term.lines[start.y];
    }
  }
}

void
term_paint(void)
{
 /* The display line that the cursor is on, or -1 if the cursor is invisible. */
  int curs_y =
    term.cursor_on && !term.show_other_screen
    ? term.curs.y - term.disptop : -1;

  for (int i = 0; i < term.rows; i++) {
    pos scrpos;
    scrpos.y = i + term.disptop;

   /* Do Arabic shaping and bidi. */
    termline *line = fetch_line(scrpos.y);
    termchar *chars = term_bidi_line(line, i);
    int *backward = chars ? term.post_bidi_cache[i].backward : 0;
    int *forward = chars ? term.post_bidi_cache[i].forward : 0;
    chars = chars ?: line->chars;

    termline *displine = term.displines[i];
    termchar *dispchars = displine->chars;
    termchar newchars[term.cols];

  /*
    * First loop: work along the line deciding what we want
    * each character cell to look like.
    */
    for (int j = 0; j < term.cols; j++) {
      termchar *d = chars + j;
      scrpos.x = backward ? backward[j] : j;
      wchar tchar = d->chr;
      uint tattr = d->attr;
      
     /* Many Windows fonts don't have the Unicode hyphen, but groff
      * uses it for man pages, so display it as the ASCII version.
      */
      if (tchar == 0x2010)
        tchar = '-';

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
        if (term.has_focus && term.tblinker)
          tchar = ' ';
        tattr &= ~ATTR_BLINK;
      }

     /*
      * Check the font we'll _probably_ be using to see if 
      * the character is wide when we don't want it to be.
      */
      if (tchar != dispchars[j].chr ||
          tattr != (dispchars[j].attr & ~(ATTR_NARROW | DATTR_MASK))) {
        if ((tattr & ATTR_WIDE) == 0 && win_char_width(tchar) == 2)
          tattr |= ATTR_NARROW;
      }
      else if (dispchars[j].attr & ATTR_NARROW)
        tattr |= ATTR_NARROW;

     /* FULL-TERMCHAR */
      newchars[j].attr = tattr;
      newchars[j].chr = tchar;
     /* Combining characters are still read from chars */
      newchars[j].cc_next = 0;
    }

    if (i == curs_y) {
     /* Determine the column the cursor is on, taking bidi into account and
      * moving it one column to the left when it's on the right half of a
      * wide character.
      */
      int curs_x = term.curs.x;
      if (forward)
        curs_x = forward[curs_x];
      if (curs_x > 0 && chars[curs_x].chr == UCSWIDE)
        curs_x--;

     /* Determine cursor cell attributes. */
      newchars[curs_x].attr |=
        (!term.has_focus ? TATTR_PASCURS :
         term.cblinker || !term_cursor_blinks() ? TATTR_ACTCURS : 0) |
        (term.curs.wrapnext ? TATTR_RIGHTCURS : 0);
      
      if (term.cursor_invalid)
        dispchars[curs_x].attr |= ATTR_INVALID;
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
    int laststart = 0;
    bool dirtyrect = false;
    for (int j = 0; j < term.cols; j++) {
      if (dispchars[j].attr & DATTR_STARTRUN) {
        laststart = j;
        dirtyrect = false;
      }

      if (dispchars[j].chr != newchars[j].chr ||
          (dispchars[j].attr & ~DATTR_STARTRUN) != newchars[j].attr) {
        if (!dirtyrect) {
          for (int k = laststart; k < j; k++)
            dispchars[k].attr |= ATTR_INVALID;
          dirtyrect = true;
        }
      }

      if (dirtyrect)
        dispchars[j].attr |= ATTR_INVALID;
    }

   /*
    * Finally, loop once more and actually do the drawing.
    */
    wchar text[max(term.cols, 16)];
    int textlen = 0;
    bool dirty_run = (line->attr != displine->attr);
    bool dirty_line = dirty_run;
    uint attr = 0;
    int start = 0;

    displine->attr = line->attr;

    for (int j = 0; j < term.cols; j++) {
      termchar *d = chars + j;
      uint tattr = newchars[j].attr;
      wchar tchar = newchars[j].chr;

      if ((dispchars[j].attr ^ tattr) & ATTR_WIDE)
        dirty_line = true;

      bool break_run = tattr ^ attr;

     /*
      * Break on both sides of any combined-character cell.
      */
      if (d->cc_next || (j > 0 && d[-1].cc_next))
        break_run = true;

      if (!dirty_line) {
        if (dispchars[j].chr == tchar &&
            (dispchars[j].attr & ~DATTR_STARTRUN) == tattr)
          break_run = true;
        else if (!dirty_run && textlen == 1)
          break_run = true;
      }

      if (break_run) {
        if (dirty_run && textlen)
          win_text(start, i, text, textlen, attr, line->attr);
        start = j;
        textlen = 0;
        attr = tattr;
        dirty_run = dirty_line;
      }

      bool do_copy =
        !termchars_equal_override(&dispchars[j], d, tchar, tattr);
      dirty_run |= do_copy;

      text[textlen++] = tchar;

      if (d->cc_next) {
        termchar *dd = d;
        while (dd->cc_next && textlen < 16) {
          dd += dd->cc_next;
          text[textlen++] = dd->chr;
        }
        attr |= TATTR_COMBINING;
      }

      if (do_copy) {
        copy_termchar(displine, j, d);  // may change displine->chars
        dispchars = displine->chars;
        dispchars[j].chr = tchar;
        dispchars[j].attr = tattr;
        if (start == j)
          dispchars[j].attr |= DATTR_STARTRUN;
      }

     /* If it's a wide char step along to the next one. */
      if ((tattr & ATTR_WIDE) && ++j < term.cols) {
        d++;
       /*
        * By construction above, the cursor should not
        * be on the right-hand half of this character.
        * Ever.
        */
        if (!termchars_equal(&dispchars[j], d))
          dirty_run = true;
        copy_termchar(displine, j, d);
      }
    }
    if (dirty_run && textlen)
      win_text(start, i, text, textlen, attr, line->attr);
    release_line(line);
  }

  term.cursor_invalid = false;
}

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
    if ((term.displines[i]->attr & LATTR_MODE) == LATTR_NORM)
      for (int j = left; j <= right && j < term.cols; j++)
        term.displines[i]->chars[j].attr |= ATTR_INVALID;
    else
      for (int j = left / 2; j <= right / 2 + 1 && j < term.cols; j++)
        term.displines[i]->chars[j].attr |= ATTR_INVALID;
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
  win_update();
}

void
term_set_focus(bool has_focus)
{
  if (has_focus != term.has_focus) {
    term.has_focus = has_focus;
    term_schedule_cblink();
    if (term.report_focus)
      child_write(has_focus ? "\e[I" : "\e[O", 3);
  }
}

void
term_update_cs()
{
  term_cursor *curs = &term.curs;
  cs_set_mode(
    curs->oem_acs ? CSM_OEM :
    curs->utf ? CSM_UTF8 :
    curs->csets[curs->g1] == CSET_OEM ? CSM_OEM : CSM_DEFAULT
  );
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

void
term_hide_cursor(void)
{
  if (term.cursor_on) {
    term.cursor_on = false;
    win_update();
  }
}
