// termclip.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "linedisc.h"

// Clipboard data has to be NUL-terminated.
static const bool sel_nul_terminated = true;

// Copying to the clipboard terminates lines with CRLF.
static const wchar sel_nl[] = { '\r', '\n' };

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
      if (term.paste_buffer[term.paste_pos + n++] == '\r')
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
  term.sel_end = (pos){term_last_nonempty_line(), term.cols};
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
