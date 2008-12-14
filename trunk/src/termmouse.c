// termmouse.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"
#include "linedisc.h"
#include "win.h"

static pos
sel_spread_word(pos p, int dir)
{
 /*
  * In this mode, the units are maximal runs of characters
  * whose `wordness' has the same value.
  */
  termline *ldata = lineptr(p.y);
  short wvalue = wordtype(UCSGET(ldata->chars, p.x));
  if (dir == 1) {
    for (;;) {
      int maxcols =
        (ldata->lattr & LATTR_WRAPPED2 ? term.cols - 1 : term.cols);
      if (p.x < maxcols - 1) {
        if (wordtype(UCSGET(ldata->chars, p.x + 1)) == wvalue)
          p.x++;
        else
          break;
      }
      else {
        if (ldata->lattr & LATTR_WRAPPED) {
          termline *ldata2;
          ldata2 = lineptr(p.y + 1);
          if (wordtype(UCSGET(ldata2->chars, 0))
              == wvalue) {
            p.x = 0;
            p.y++;
            unlineptr(ldata);
            ldata = ldata2;
          }
          else {
            unlineptr(ldata2);
            break;
          }
        }
        else
          break;
      }
    }
  }
  else {
    int topy = -sblines();
    for (;;) {
      if (p.x > 0) {
        if (wordtype(UCSGET(ldata->chars, p.x - 1)) == wvalue)
          p.x--;
        else
          break;
      }
      else {
        termline *ldata2;
        int maxcols;
        if (p.y <= topy)
          break;
        ldata2 = lineptr(p.y - 1);
        maxcols =
          (ldata2->lattr & LATTR_WRAPPED2 ? term.cols - 1 : term.cols);
        if (ldata2->lattr & LATTR_WRAPPED) {
          if (wordtype(UCSGET(ldata2->chars, maxcols - 1))
              == wvalue) {
            p.x = maxcols - 1;
            p.y--;
            unlineptr(ldata);
            ldata = ldata2;
          }
          else {
            unlineptr(ldata2);
            break;
          }
        }
        else
          break;
      }
    }
  }
  unlineptr(ldata);
  return p;
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static pos
sel_spread_half(pos p, int dir)
{
  switch (term.mouse_state) {
    when MS_SEL_CHAR: {
     /*
      * In this mode, every character is a separate unit, except
      * for runs of spaces at the end of a non-wrapping line.
      */
      termline *ldata = lineptr(p.y);
      if (!(ldata->lattr & LATTR_WRAPPED)) {
        termchar *q = ldata->chars + term.cols;
        while (q > ldata->chars && IS_SPACE_CHR(q[-1].chr) && !q[-1].cc_next)
          q--;
        if (q == ldata->chars + term.cols)
          q--;
        if (p.x >= q - ldata->chars)
          p.x = (dir == -1 ? q - ldata->chars : term.cols - 1);
      }
      unlineptr(ldata);
    }
    when MS_SEL_WORD:
      p = sel_spread_word(p, dir); 
    when MS_SEL_LINE:
     /*
      * In this mode, every line is a unit.
      */
      p.x = (dir == -1 ? 0 : term.cols - 1);
    default:
     /* Shouldn't happen. */
      break;
  }
  return p;
}

static void
sel_spread(void)
{
  term.sel_start = sel_spread_half(term.sel_start, -1);
  decpos(term.sel_end);
  term.sel_end = sel_spread_half(term.sel_end, +1);
  incpos(term.sel_end);
}

static void
sel_drag(pos selpoint)
{
  term.selected = true;
  if (!term.sel_rect) {
   /*
    * For normal selection, we set (sel_start,sel_end) to
    * (selpoint,sel_anchor) in some order.
    */
    if (poslt(selpoint, term.sel_anchor)) {
      term.sel_start = selpoint;
      term.sel_end = term.sel_anchor;
    }
    else {
      term.sel_start = term.sel_anchor;
      term.sel_end = selpoint;
    }
    incpos(term.sel_end);
    sel_spread();
  }
  else {
   /*
    * For rectangular selection, we may need to
    * interchange x and y coordinates (if the user has
    * dragged in the -x and +y directions, or vice versa).
    */
    term.sel_start.x = min(term.sel_anchor.x, selpoint.x);
    term.sel_end.x = 1 + max(term.sel_anchor.x, selpoint.x);
    term.sel_start.y = min(term.sel_anchor.y, selpoint.y);
    term.sel_end.y = max(term.sel_anchor.y, selpoint.y);
  }
}

static void
sel_extend(pos selpoint)
{
  if (term.selected) {
    if (!term.sel_rect) {
     /*
      * For normal selection, we extend by moving
      * whichever end of the current selection is closer
      * to the mouse.
      */
      if (posdiff(selpoint, term.sel_start) <
          posdiff(term.sel_end, term.sel_start) / 2) {
        term.sel_anchor = term.sel_end;
        decpos(term.sel_anchor);
      }
      else
        term.sel_anchor = term.sel_start;
    }
    else {
     /*
      * For rectangular selection, we have a choice of
      * _four_ places to put sel_anchor and selpoint: the
      * four corners of the selection.
      */
      term.sel_anchor.x = 
        selpoint.x * 2 < term.sel_start.x + term.sel_end.x
        ? term.sel_end.x - 1
        : term.sel_start.x;
      term.sel_anchor.y = 
        selpoint.y * 2 < term.sel_start.y + term.sel_end.y
        ? term.sel_end.y
        : term.sel_start.y;
    }
  }
  else
    term.sel_anchor = selpoint;
  sel_drag(selpoint);
}

static void
send_mouse_event(char code, mod_keys mods, pos p)
{
  char buf[6] = "\e[M";
  buf[3] = code | (mods & ~CTRL);  // ctrl is ignored
  buf[4] = p.x + 33;
  buf[5] = p.y + 33;
  ldisc_send(buf, 6, 0);
}

static pos
box_pos(pos p)
{
  p.y = box(0, p.y, term.rows - 1);
  p.x = box(0, p.x, term.cols - 1);
  return p;
}

static pos
get_selpoint(pos p)
{
  pos selpoint;
  selpoint.y = p.y + term.disptop;
  termline *ldata = lineptr(selpoint.y);

  if ((ldata->lattr & LATTR_MODE) != LATTR_NORM)
    p.x /= 2;

 /*
  * Transform x through the bidi algorithm to find the _logical_
  * click point from the physical one.
  */
  if (term_bidi_line(ldata, p.y) != null)
    p.x = term.post_bidi_cache[p.y].backward[p.x];
  selpoint.x = p.x;
  unlineptr(ldata);
  return selpoint;
}

void
term_mouse_click(mouse_button b, mod_keys mods, pos p, int count)
{
  p = box_pos(p);
  if (mods & CTRL) {
    if (term.mouse_tracking) {
      if (term.mouse_tracking == MT_X10)
        mods = 0;
      send_mouse_event(0x1F + b, mods, p);
    }
    term.mouse_state = MS_CLICKED;
    return;
  }
  pos selpoint = get_selpoint(p);
  if (b == MB_LEFT) {
    term.mouse_state = count;
    if (count == 1) {
      term.sel_rect = mods & ALT;
      if (mods & SHIFT) {
        // Shift click: extend selection
        term.mouse_state = MS_SEL_CHAR;
        sel_extend(selpoint);
      }
      else {
        // Plain click: clear/start selection
        term.selected = false;
        term.sel_start = term.sel_end = (pos){0, 0};
        term.sel_anchor = selpoint;
      }
    }
    else {
      // Double or triple-click: select whole word or line
      term.selected = true;
      term.sel_rect = false;
      term.sel_start = term.sel_end = term.sel_anchor = selpoint;
      incpos(term.sel_end);
      sel_spread();
    }
  }
  else if (b == MB_MIDDLE) {
    if (mods & SHIFT)
      term_copy();
    else
      term_paste();
  }
  else if (b == MB_RIGHT)
    term_paste();
  term_update();
}

void
term_mouse_release(mouse_button unused(b), mod_keys mods, pos p)
{
  p = box_pos(p);
  if (term.mouse_state == MS_CLICKED) {
    if (term.mouse_tracking >= MT_VT200)
      send_mouse_event(0x23, mods, p);
  }
  else if (term_selecting() && cfg.copy_on_select)
    term_copy();
  term.mouse_state = MS_IDLE;
}

static void
sel_scroll_cb(void)
{
  if (term_selecting() && term.sel_scroll != 0) {
    term_scroll(0, term.sel_scroll);
    sel_drag(get_selpoint(term.sel_pos));
    win_set_timer(sel_scroll_cb, 200);
  }
}

void
term_mouse_move(mouse_button b, mod_keys mods, pos p)
{
  pos bp = box_pos(p);
  if (term_selecting()) {
    if (bp.y == p.y)
      term.sel_scroll = 0;
    else {
      if (term.sel_scroll == 0) 
        win_set_timer(sel_scroll_cb, 200);
      term.sel_scroll = p.y < 0 ? p.y : p.y - term.rows + 1;
      term.sel_pos = bp;
    }
    sel_drag(get_selpoint(bp));
    term_update();
  }
  else {
    if (term.mouse_state == MS_CLICKED) {
      if (term.mouse_tracking >= MT_BTN_EVENT)
        send_mouse_event(0x3F + b, mods, bp);
    }
    else {
      if (term.mouse_tracking == MT_ANY_EVENT)
        send_mouse_event(0x43, mods, bp);
    }
  }
}

void
term_mouse_wheel(int lines, mod_keys mods, pos p)
{
  if (mods & (ALT | CTRL))
    return; // reserved for future use
  if (term.which_screen == 0)
    term_scroll(0, lines * (mods & SHIFT ? term.rows : 1));
  else if (term.mouse_tracking) {
    // Send as mouse codes.
    char code = 0x60 | (lines > 0);
    for (int i = 0; i < abs(lines); i++)
      send_mouse_event(code, mods, p);
  }
  else {
    // Send as cursor keys with ctrl modifier.
    char code[6] = "\e[1;1~";
    if (term.app_cursor_keys)
      code[1] = 'O';
    code[4] = '1' + cfg.scroll_mod;
    if (mods & SHIFT)
      code[2] = lines < 0 ? '5' : '6';  // PgUp/PgDown
    else
      code[5] = lines < 0 ? 'A' : 'B';  // Arrow up/down
    for (int i = 0; i < abs(lines); i++)
      ldisc_send(code, 6, 1);  
  }
}
