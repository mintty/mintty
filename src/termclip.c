// termclip.c (part of mintty)
// Copyright 2008-10 Andy Koppe, 2017 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "child.h"
#include "charset.h"

typedef struct {
  size_t capacity;  // number of items allocated for text/cattrs
  size_t len;    // number of actual items at text/cattrs (inc. null terminator)
  wchar *text;   // text to copy (eventually null terminated)
  cattr *cattrs; // matching cattr for each wchar of text
} clip_workbuf;

static void
destroy_clip_workbuf(clip_workbuf * b)
{
  assert(b && b->capacity); // we're only called after get_selection, which always allocates
  free(b->text);
  free(b->cattrs);
  free(b);
}

// All b members must be 0 initially, ca may be null if the caller doesn't care
static void
clip_addchar(clip_workbuf * b, wchar chr, cattr * ca)
{
  if (b->len >= b->capacity) {
    b->capacity = b->len ? b->len * 2 : 1024;  // x2 strategy, 1K chars initially
    b->text = renewn(b->text, b->capacity);
    b->cattrs = renewn(b->cattrs, b->capacity);
  }

  b->text[b->len] = chr;
  b->cattrs[b->len] = ca ? *ca : (cattr){0, 0, 0};
  b->len++;
}

// except OOM, guaranteed at least emtpy null terminated wstring and one cattr
static clip_workbuf*
get_selection(pos start, pos end, bool rect)
{
  int old_top_x = start.x;    /* needed for rect==1 */
  clip_workbuf *buf = newn(clip_workbuf, 1);
  *buf = (clip_workbuf){0, 0, 0, 0};  // all members to 0 initially

  while (poslt(start, end)) {
    bool nl = false;
    termline *line = fetch_line(start.y);
    pos nlpos;
    wchar * sixel_clipp = (wchar *)cfg.sixel_clip_char;

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
    if (!(line->lattr & LATTR_WRAPPED)) {
      while (nlpos.x && line->chars[nlpos.x - 1].chr == ' ' &&
             !line->chars[nlpos.x - 1].cc_next && poslt(start, nlpos))
        decpos(nlpos);
      if (poslt(nlpos, end))
        nl = true;
    }
    else if (line->lattr & LATTR_WRAPPED2) {
     /* Ignore the last char on the line in a WRAPPED2 line. */
      decpos(nlpos);
    }

   /*
    * ... and then clip it to the terminal x coordinate if
    * we're doing rectangular selection. (In this case we
    * still did the above, so that copying e.g. the right-hand
    * column from a table doesn't fill with spaces on the right.)
    */
    if (rect) {
      if (nlpos.x > end.x)
        nlpos.x = end.x;
      nl = (start.y < end.y);
    }

    while (poslt(start, end) && poslt(start, nlpos)) {
      wchar cbuf[16], *p;
      int x = start.x;

      if (line->chars[x].chr == UCSWIDE) {
        start.x++;
        continue;
      }

      while (1) {
        wchar c = line->chars[x].chr;
        cattr *pca = &line->chars[x].attr;
        if (c == SIXELCH && *cfg.sixel_clip_char) {
          // copy replacement into clipboard
          if (!*sixel_clipp)
            sixel_clipp = (wchar *)cfg.sixel_clip_char;
          c = *sixel_clipp++;
        }
        else
          sixel_clipp = (wchar *)cfg.sixel_clip_char;
        cbuf[0] = c;
        cbuf[1] = 0;

        for (p = cbuf; *p; p++)
          clip_addchar(buf, *p, pca);

        if (line->chars[x].cc_next)
          x += line->chars[x].cc_next;
        else
          break;
      }
      start.x++;
    }
    if (nl) {
      clip_addchar(buf, '\r', 0);
      clip_addchar(buf, '\n', 0);
    }
    start.y++;
    start.x = rect ? old_top_x : 0;

    release_line(line);
  }
  clip_addchar(buf, 0, 0);
  return buf;
}

void
term_copy(void)
{
  if (!term.selected)
    return;

  clip_workbuf *buf = get_selection(term.sel_start, term.sel_end, term.sel_rect);
  win_copy(buf->text, buf->cattrs, buf->len);
  destroy_clip_workbuf(buf);
}

void
term_open(void)
{
  if (!term.selected)
    return;
  clip_workbuf *buf = get_selection(term.sel_start, term.sel_end, term.sel_rect);

  // Don't bother opening if it's all whitespace.
  wchar *p = buf->text;
  while (iswspace(*p))
    p++;
  if (*p)
    win_open(wcsdup(buf->text));  // win_open frees its argument

  destroy_clip_workbuf(buf);
}

void
term_paste(wchar *data, uint len)
{
  term_cancel_paste();

  term.paste_buffer = newn(wchar, len);
  term.paste_len = term.paste_pos = 0;

  // Copy data to the paste buffer, converting both Windows-style \r\n and
  // Unix-style \n line endings to \r, because that's what the Enter key sends.
  for (uint i = 0; i < len; i++) {
    wchar wc = data[i];
    if (wc != '\n')
      term.paste_buffer[term.paste_len++] = wc;
    else if (i == 0 || data[i - 1] != '\r')
      term.paste_buffer[term.paste_len++] = '\r';
  }

  if (term.bracketed_paste)
    child_write("\e[200~", 6);
  term_send_paste();
}

void
term_cancel_paste(void)
{
  if (term.paste_buffer) {
    free(term.paste_buffer);
    term.paste_buffer = 0;
    if (term.bracketed_paste)
      child_write("\e[201~", 6);
  }
}

void
term_send_paste(void)
{
  int i = term.paste_pos;
  while (i < term.paste_len && term.paste_buffer[i++] != '\r');
  child_sendw(term.paste_buffer + term.paste_pos, i - term.paste_pos);
  if (i < term.paste_len)
    term.paste_pos = i;
  else
    term_cancel_paste();
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

#define dont_debug_user_cmd_clip

static wchar *
term_get_text(bool all, bool screen, bool command)
{
  pos start;
  pos end;
  bool rect = false;

  if (command) {
    int sbtop = -sblines();
    int y = term_last_nonempty_line();
    bool skipprompt = true;  // skip upper lines of multi-line prompt

    if (y < sbtop) {
      y = sbtop;
      end = (pos){y, 0};
    }
    else {
      termline * line = fetch_line(y);
      if (line->lattr & LATTR_MARKED) {
        if (y > sbtop) {
          y--;
          end = (pos){y, term.cols};
          termline * line = fetch_line(y);
          if (line->lattr & LATTR_MARKED)
            y++;
        }
        else {
          end = (pos){y, 0};
        }
      }
      else {
        skipprompt = line->lattr & LATTR_UNMARKED;
        end = (pos){y, term.cols};
      }

      if (fetch_line(y)->lattr & LATTR_UNMARKED)
        end = (pos){y, 0};
    }

    int yok = y;
    while (y-- > sbtop) {
      termline * line = fetch_line(y);
#ifdef debug_user_cmd_clip
      printf("y %d skip %d marked %X\n", y, skipprompt, line->lattr & (LATTR_UNMARKED | LATTR_MARKED));
#endif
      if (skipprompt && (line->lattr & LATTR_UNMARKED))
        end = (pos){y, 0};
      else
        skipprompt = false;
      if (line->lattr & LATTR_MARKED) {
        break;
      }
      yok = y;
    }
    start = (pos){yok, 0};
#ifdef debug_user_cmd_clip
    printf("%d:%d...%d:%d\n", start.y, start.x, end.y, end.x);
#endif
  }
  else if (screen) {
    start = (pos){term.disptop, 0};
    end = (pos){term_last_nonempty_line(), term.cols};
  }
  else if (all) {
    start = (pos){-sblines(), 0};
    end = (pos){term_last_nonempty_line(), term.cols};
  }
  else if (!term.selected) {
    return wcsdup(W(""));
  }
  else {
    start = term.sel_start;
    end = term.sel_end;
    rect = term.sel_rect;
  }

  clip_workbuf *buf = get_selection(start, end, rect);
  wchar * tbuf = wcsdup(buf->text);
  destroy_clip_workbuf(buf);
  return tbuf;
}

void
term_cmd(char * cmdpat)
{
  // provide scrollback buffer
  wchar * wsel = term_get_text(true, false, false);
  char * sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_BUFFER", sel, true);
  free(sel);
  // provide current selection
  wsel = term_get_text(false, false, false);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_SELECT", sel, true);
  free(sel);
  // provide current screen
  wsel = term_get_text(false, true, false);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_SCREEN", sel, true);
  free(sel);
  // provide last command output
  wsel = term_get_text(false, false, true);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_OUTPUT", sel, true);
  free(sel);
  // provide window title
  char * ttl = win_get_title();
  setenv("MINTTY_TITLE", ttl, true);
  free(ttl);

#ifdef use_placeholders
  sel = 0;
  if (strstr(cmdpat, "%s") || strstr(cmdpat, "%1$s")) {
    wchar * wsel = term_get_text(false, false, false);
    sel = cs__wcstombs(wsel);
    free(wsel);
  }

  int len = strlen(cmdpat) + (sel ? strlen(sel) : 0) + 1;
  char * cmd = newn(char, len);
  sprintf(cmd, cmdpat, sel ?: "");
  if (sel)
    free(sel);
#else
  char * cmd = cmdpat;
#endif

  FILE * cmdf = popen(cmd, "r");
  unsetenv("MINTTY_TITLE");
  unsetenv("MINTTY_OUTPUT");
  unsetenv("MINTTY_SCREEN");
  unsetenv("MINTTY_SELECT");
  unsetenv("MINTTY_BUFFER");
  unsetenv("MINTTY_CWD");
  unsetenv("MINTTY_PID");
  unsetenv("MINTTY_PROG");
  if (cmdf) {
    if (term.bracketed_paste)
      child_write("\e[200~", 6);
    char line[222];
    while (fgets(line, sizeof line, cmdf)) {
      child_send(line, strlen(line));
    }
    pclose(cmdf);
    if (term.bracketed_paste)
      child_write("\e[201~", 6);
  }
}

