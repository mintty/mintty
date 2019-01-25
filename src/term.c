// term.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2016-2018 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "winimg.h"
#include "charset.h"
#include "child.h"
#include "winsearch.h"
#if CYGWIN_VERSION_API_MINOR >= 66
#include <langinfo.h>
#endif


struct term term;

typedef struct {
  termline ** buf;
  int start;
  int length;
  int capacity;
} circbuf;

enum {
  NO_UPDATE = 0,
  PARTIAL_UPDATE = 1,
  FULL_UPDATE = 2
};

static int markpos = 0;
static bool markpos_valid = false;

const cattr CATTR_DEFAULT =
            {.attr = ATTR_DEFAULT,
             .truefg = 0, .truebg = 0, .ulcolr = (colour)-1,
             .link = -1
            };

termchar basic_erase_char =
   {.cc_next = 0, .chr = ' ',
            /* CATTR_DEFAULT */
    .attr = {.attr = ATTR_DEFAULT | TATTR_CLEAR,
             .truefg = 0, .truebg = 0, .ulcolr = (colour)-1,
             .link = -1
            }
   };


#define dont_debug_hyperlinks

static char * * links = 0;
static int nlinks = 0;
static int linkid = 0;

int
putlink(char * link)
{
#if CYGWIN_VERSION_API_MINOR >= 66
  bool utf8 = strcmp(nl_langinfo(CODESET), "UTF-8") == 0;
#else
  bool utf8 = strstr(cs_get_locale(), ".65001");
#endif
  if (!utf8) {
    wchar * wlink = cs__mbstowcs(link);
    link = cs__wcstoutf(wlink);
    free(wlink);
  }

  if (*link != ';')
    for (int i = 0; i < nlinks; i++)
      if (0 == strcmp(link, links[i])) {
        if (!utf8)
          free(link);
        return i;
      }

  char * link1;
  if (*link == ';')
    link1 = asform("=%d%s", ++linkid, link);
  else
    link1 = strdup(link);
#ifdef debug_hyperlinks
  printf("[%d] link <%s>\n", nlinks, link1);
#endif
  if (!utf8)
    free(link);

  nlinks++;
  links = renewn(links, nlinks);
  links[nlinks - 1] = link1;
  return nlinks - 1;
}

char *
geturl(int n)
{
  if (n >= 0 && n < nlinks) {
    char * url = strchr(links[n], ';');
    if (url) {
      url++;
#ifdef debug_hyperlinks
      printf("[%d] url <%s> link <%s>\n", n, url, links[n]);
#endif
      return url;
    }
  }
  return 0;
}


static bool
vt220(string term)
{
  char * vt = strstr(term, "vt");
  if (vt) {
    unsigned int ver;
    if (sscanf(vt + 2, "%u", &ver) == 1 && ver >= 220)
      return true;
  }
  return false;
}

/*
 * Call when the terminal's blinking-text settings change, or when
 * a text blink has just occurred.
 */
static void term_schedule_tblink(void);
static void term_schedule_tblink2(void);

static void
tblink_cb(void)
{
  term.tblinker = !term.tblinker;
  term_schedule_tblink();
  win_update(false);
}

static void
term_schedule_tblink(void)
{
  if (term.blink_is_real)
    win_set_timer(tblink_cb, 500);
  else
    term.tblinker = 1;  /* reset when not in use */
}

static void
tblink2_cb(void)
{
  term.tblinker2 = !term.tblinker2;
  term_schedule_tblink2();
  win_update(false);
}

static void
term_schedule_tblink2(void)
{
  if (term.blink_is_real)
    win_set_timer(tblink2_cb, 300);
  else
    term.tblinker2 = 1;  /* reset when not in use */
}

/*
 * Likewise with cursor blinks.
 */
static void
cblink_cb(void)
{
  term.cblinker = !term.cblinker;
  term_schedule_cblink();
  win_update(false);
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
  win_update(false);
}

void
term_schedule_vbell(int already_started, int startpoint)
{
  int ticks_gone = already_started ? get_tick_count() - startpoint : 0;
  int ticks = 141 - ticks_gone;
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
  curs->attr = CATTR_DEFAULT;
  curs->gl = 0;
  curs->gr = 0;
  curs->oem_acs = 0;
  curs->utf = false;
  for (uint i = 0; i < lengthof(curs->csets); i++)
    curs->csets[i] = CSET_ASCII;
  curs->cset_single = CSET_ASCII;
  curs->decnrc_enabled = false;

  curs->autowrap = true;
  curs->rev_wrap = cfg.old_wrapmodes;
  curs->bidi = 0;

  curs->origin = false;
}

void
term_reset(bool full)
{
  if (term.cmd_buf == NULL) {
    term.cmd_buf = newn(char, 128);
    term.cmd_buf_cap = 128;
  }

  term.state = NORMAL;

  // DECSTR attributes and cursor states to be reset
  term_cursor_reset(&term.curs);
  term_cursor_reset(&term.saved_cursors[0]);
  term_cursor_reset(&term.saved_cursors[1]);
  term_update_cs();
  term.erase_char = basic_erase_char;

  // DECSTR states to be reset (in addition to cursor states)
  // https://www.vt100.net/docs/vt220-rm/table4-10.html
  term.cursor_on = true;
  term.insert = false;
  term.marg_top = 0;
  term.marg_bot = term.rows - 1;
  term.app_cursor_keys = false;

  if (full) {
    term.deccolm_allowed = cfg.enable_deccolm_init;  // not reset by xterm
    term.vt220_keys = vt220(cfg.term);  // not reset by xterm
    term.app_keypad = false;  // xterm only with RIS
    term.app_wheel = false;
    term.app_control = 0;
  }
  term.modify_other_keys = 0;  // xterm resets this

  term.backspace_sends_bs = cfg.backspace_sends_bs;  // xterm only with RIS
  term.delete_sends_del = cfg.delete_sends_del;  // not reset by xterm
  if (full && term.tabs) {
    for (int i = 0; i < term.cols; i++)
      term.tabs[i] = (i % 8 == 0);
  }
  if (full) {
    term.rvideo = 0;  // not reset by xterm
    term.bell_taskbar = cfg.bell_taskbar;  // not reset by xterm
    term.bell_popup = cfg.bell_popup;  // not reset by xterm
    term.mouse_mode = MM_NONE;
    term.mouse_enc = ME_X10;
    term.locator_by_pixels = false;
    term.locator_1_enabled = false;
    term.locator_report_up = false;
    term.locator_report_dn = false;
    term.locator_rectangle = false;
    term.report_focus = 0;  // xterm only with RIS
    term.report_font_changed = 0;
    term.report_ambig_width = 0;
    term.shortcut_override = term.escape_sends_fs = term.app_escape_key = false;
    term.wheel_reporting = true;
    term.echoing = false;
    term.bracketed_paste = false;
    term.wide_indic = false;
    term.wide_extra = false;
    term.disable_bidi = false;
    term.enable_bold_colour = cfg.bold_as_colour;
  }

  term.virtuallines = 0;
  term.altvirtuallines = 0;
  term.imgs.parser_state = NULL;
  term.imgs.first = NULL;
  term.imgs.last = NULL;
  term.imgs.altfirst = NULL;
  term.imgs.altlast = NULL;
  term.sixel_display = 0;
  term.sixel_scrolls_right = 0;
  term.sixel_scrolls_left = 0;

  term.cursor_type = -1;
  term.cursor_blinks = -1;
  if (full) {
    term.blink_is_real = cfg.allow_blinking;
    term.hide_mouse = cfg.hide_mouse;
  }

  if (full) {
    term.selected = false;
    term.hovering = false;
    term.hoverlink = -1;
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
  }

  term.in_vbell = false;
  term_schedule_tblink();
  term_schedule_tblink2();
  term_schedule_cblink();
  term_clear_scrollback();

  win_reset_colours();
}

static void
show_screen(bool other_screen, bool flip)
{
  term.show_other_screen = other_screen;
  term.disptop = 0;
  if (flip || cfg.input_clears_selection)
    term.selected = false;

  // Reset cursor blinking.
  if (!other_screen) {
    term.cblinker = 1;
    term_schedule_cblink();
  }

  win_update(false);
}

/* Return to active screen and reset scrollback */
void
term_reset_screen(void)
{
  show_screen(false, false);
}

/* Switch display to other screen and reset scrollback */
void
term_flip_screen(void)
{
  show_screen(!term.show_other_screen, true);
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
  term_schedule_tblink2();
  term_schedule_cblink();
  if (new_cfg.backspace_sends_bs != cfg.backspace_sends_bs)
    term.backspace_sends_bs = new_cfg.backspace_sends_bs;
  if (new_cfg.delete_sends_del != cfg.delete_sends_del)
    term.delete_sends_del = new_cfg.delete_sends_del;
  if (new_cfg.bell_taskbar != cfg.bell_taskbar)
    term.bell_taskbar = new_cfg.bell_taskbar;
  if (new_cfg.bell_popup != cfg.bell_popup)
    term.bell_popup = new_cfg.bell_popup;
  if (strcmp(new_cfg.term, cfg.term))
    term.vt220_keys = vt220(new_cfg.term);
}

static bool
in_result(pos abspos, result run)
{
  return
    (abspos.x + abspos.y * term.cols >= run.x + run.y * term.cols) &&
    (abspos.x + abspos.y * term.cols <  run.x + run.y * term.cols + run.len);
}

static bool
in_results_recurse(pos abspos, int lo, int hi)
{
  if (hi - lo == 0) {
    return false;
  }
  int mid = (lo + hi) / 2;
  result run = term.results.results[mid];
  if (run.x + run.y * term.cols > abspos.x + abspos.y * term.cols) {
    return in_results_recurse(abspos, lo, mid);
  } else if (run.x + run.y * term.cols + run.len <= abspos.x + abspos.y * term.cols) {
    return in_results_recurse(abspos, mid + 1, hi);
  }
  return true;
}

static int
in_results(pos scrpos)
{
  if (term.results.length == 0) {
    return 0;
  }

  pos abspos = {
    .x = scrpos.x,
    .y = scrpos.y + term.sblines
  };

  int match = in_results_recurse(abspos, 0, term.results.length);
  match += in_result(abspos, term.results.results[term.results.current]);
  return match;
}

static void
results_add(result abspos)
{
  assert(term.results.capacity > 0);
  if (term.results.length == term.results.capacity) {
    term.results.capacity *= 2;
    term.results.results = renewn(term.results.results, term.results.capacity);
  }

  term.results.results[term.results.length] = abspos;
  ++term.results.length;
}

static void
results_partial_clear(int pos)
{
  int i = term.results.length;
  while (i > 0 && term.results.results[i - 1].y >= pos) {
    --i;
  }
  term.results.length = i;
}

void
term_set_search(wchar * needle)
{
  free(term.results.query);
  term.results.query = needle;

  // transform UTF-16 to UCS for matching
  int wlen = wcslen(needle);
  xchar * xquery = malloc(sizeof(xchar) * (wlen + 1));
  wchar prev = 0;
  int xlen = -1;
  for (int i = 0; i < wlen; i++) {
    if ((prev & 0xFC00) == 0xD800 && (needle[i] & 0xFC00) == 0xDC00)
      xquery[xlen] = ((xchar) (prev - 0xD7C0) << 10) | (needle[i] & 0x03FF);
    else
      xquery[++xlen] = needle[i];
    prev = needle[i];
  }
  xquery[++xlen] = 0;

  free(term.results.xquery);
  term.results.xquery = xquery;
  term.results.xquery_length = xlen;
  term.results.update_type = FULL_UPDATE;
}

static void
circbuf_init(circbuf * cb, int sz)
{
  cb->capacity = sz;
  cb->length = 0;
  cb->start = 0;
  cb->buf = newn(termline*, sz);
}

static void
circbuf_destroy(circbuf * cb)
{
  cb->capacity = 0;
  cb->length = 0;
  cb->start = 0;

  // Free any termlines we have left.
  for (int i = 0; i < cb->capacity; ++i) {
    if (cb->buf[i] == NULL)
      continue;
    release_line(cb->buf[i]);
  }
  free(cb->buf);
  cb->buf = NULL;
}

static void
circbuf_push(circbuf * cb, termline * tl)
{
  int pos = (cb->start + cb->length) % cb->capacity;

  if (cb->length < cb->capacity) {
    ++cb->length;
  } else {
    ++cb->start;
    release_line(cb->buf[pos]);
  }
  cb->buf[pos] = tl;
}

static termline *
circbuf_get(circbuf * cb, int i)
{
  assert(i < cb->length);
  return cb->buf[(cb->start + i) % cb->capacity];
}

#ifdef dynamic_casefolding
static struct {
  uint code, fold;
} * case_folding;
static int case_foldn = 0;

static void
init_case_folding()
{
  static bool init = false;
  if (init)
    return;
  init = true;

  FILE * cf = fopen("/usr/share/unicode/ucd/CaseFolding.txt", "r");
  if (cf) {
    uint last = 0;
    case_folding = newn(typeof(* case_folding), 1);
    char buf[100];
    while (fgets(buf, sizeof(buf), cf)) {
      uint code, fold;
      char status;
      if (sscanf(buf, "%X; %c; %X;", &code, &status, &fold) == 3) {
        //1E9B; C; 1E61; # LATIN SMALL LETTER LONG S WITH DOT ABOVE
        //1E9E; F; 0073 0073; # LATIN CAPITAL LETTER SHARP S
        //1E9E; S; 00DF; # LATIN CAPITAL LETTER SHARP S
        //0130; T; 0069; # LATIN CAPITAL LETTER I WITH DOT ABOVE
        if (status == 'C' || status == 'S' || (status == 'T' && code != last)) {
          last = code;
          case_folding = renewn(case_folding, case_foldn + 1);
          case_folding[case_foldn].code = code;
          case_folding[case_foldn].fold = fold;
          case_foldn++;
#ifdef debug_case_folding
          printf("  {0x%04X, 0x%04X},\n", code, fold);
#endif
        }
      }
    }
    fclose(cf);
  }
}
#else
static struct {
  uint code, fold;
} case_folding[] = {
#include "casefold.t"
};
#define case_foldn lengthof(case_folding)
#define init_case_folding()
#endif

static uint
case_fold(uint ch)
{
  // binary search in table
  int min = 0;
  int max = case_foldn - 1;
  int mid;
  while (max >= min) {
    mid = (min + max) / 2;
    if (case_folding[mid].code < ch) {
      min = mid + 1;
    } else if (case_folding[mid].code > ch) {
      max = mid - 1;
    } else {
      return case_folding[mid].fold;
    }
  }
  return ch;
}

void
term_update_search(void)
{
  init_case_folding();

  int update_type = term.results.update_type;
  if (term.results.update_type == NO_UPDATE)
    return;
  term.results.update_type = NO_UPDATE;

  if (term.results.xquery_length == 0) {
    term_clear_search();
    return;
  }

  circbuf cb;
  // Allocate room for the circular buffer of termlines.
  int lcurr = 0;
  if (update_type == PARTIAL_UPDATE) {
    // How much of the backscroll we need to update on a partial update?
    // Do a ceil: (x + y - 1) / y
    // On xquery_length - 1
    int pstart = -((term.results.xquery_length + term.cols - 2) / term.cols) + term.sblines;
    lcurr = lcurr > pstart ? lcurr:pstart;
    results_partial_clear(lcurr);
  } else {
    term_clear_results();
  }
  int llen = term.results.xquery_length / term.cols + 1;
  if (llen < 2)
    llen = 2;

  circbuf_init(&cb, llen);

  // Fill in our starting set of termlines.
  for (int i = lcurr; i < term.rows + term.sblines && cb.length < cb.capacity; ++i) {
    circbuf_push(&cb, fetch_line(i - term.sblines));
  }

  int cpos = term.cols * lcurr;
  /* the number of matched chars in the current run */
  int npos = 0;
  /* the number of matched cells in the current run (anpos >= npos) */
  int anpos = 0;
  int end = term.cols * (term.rows + term.sblines);

  // Loop over every character and search for query.
  while (cpos < end) {
    // Determine the current position.
    int x = (cpos % term.cols);
    int y = (cpos / term.cols);

    // If our current position isn't in the buffer, add it in.
    if (y - lcurr >= llen) {
      circbuf_push(&cb, fetch_line(lcurr + llen - term.sblines));
      ++lcurr;
    }
    termline * lll = circbuf_get(&cb, y - lcurr);
    termchar * chr = lll->chars + x;

    if (npos == 0 && cpos + term.results.xquery_length >= end)
      break;

    xchar ch = chr->chr;
    if ((ch & 0xFC00) == 0xD800 && chr->cc_next) {
      termchar * cc = chr + chr->cc_next;
      if ((cc->chr & 0xFC00) == 0xDC00) {
        ch = ((xchar) (ch - 0xD7C0) << 10) | (cc->chr & 0x03FF);
      }
    }
    xchar pat = term.results.xquery[npos];
    bool match = case_fold(ch) == case_fold(pat);
    if (!match) {
      // Skip the second cell of any wide characters
      if (ch == UCSWIDE) {
        ++anpos;
        ++cpos;
        continue;
      }
      cpos -= npos - 1;
      npos = 0;
      anpos = 0;
      continue;
    }

    ++anpos;
    ++npos;

    if (npos >= term.results.xquery_length) {
      int start = cpos - anpos + 1;
      result run = {
        .x = start % term.cols,
        .y = start / term.cols,
        .len = anpos
      };
#ifdef debug_search
      printf("%d, %d, %d\n", run.x, run.y, run.len);
#endif
      results_add(run);
      npos = 0;
      anpos = 0;
    }

    ++cpos;
  }

  circbuf_destroy(&cb);
}

void
term_schedule_search_update(void)
{
  term.results.update_type = FULL_UPDATE;
}

void
term_schedule_search_partial_update(void)
{
  if (term.results.update_type == NO_UPDATE) {
    term.results.update_type = PARTIAL_UPDATE;
  }
}

void
term_clear_results(void)
{
  term.results.results = renewn(term.results.results, 16);
  term.results.current = 0;
  term.results.length = 0;
  term.results.capacity = 16;
}

void
term_clear_search(void)
{
  term_clear_results();
  term.results.update_type = NO_UPDATE;
  free(term.results.query);
  free(term.results.xquery);
  term.results.query = NULL;
  term.results.xquery = NULL;
  term.results.xquery_length = 0;
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
  trace_resize(("--- term_resize %d %d\n", newrows, newcols));
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
      term.virtuallines++;
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

    // Adjust image position
    term.virtuallines += min(0, store);
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

    // Adjust image position
    term.virtuallines -= restore;
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
    for (int j = 0; j < newcols; j++) {
      line->chars[j].attr = CATTR_DEFAULT;
      line->chars[j].attr.attr = ATTR_INVALID;
    }
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
  term.rows0 = newrows;
  term.cols0 = newcols;

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
  imglist *first, *last;
  long long int offset;

  if (to_alt == term.on_alt_screen)
    return;

  term.on_alt_screen = to_alt;

  termlines *oldlines = term.lines;
  term.lines = term.other_lines;
  term.other_lines = oldlines;

  /* swap image list */
  first = term.imgs.first;
  last = term.imgs.last;
  offset = term.virtuallines;
  term.imgs.first = term.imgs.altfirst;
  term.imgs.last = term.imgs.altlast;
  term.virtuallines = term.altvirtuallines;
  term.imgs.altfirst = first;
  term.imgs.altlast = last;
  term.altvirtuallines = offset;

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
  if (x <= 0 || x > term.cols)
    return;

  termline *line = term.lines[y];
  if (x == term.cols)
    line->lattr &= ~LATTR_WRAPPED2;
  else if (line->chars[x].chr == UCSWIDE) {
    clear_cc(line, x - 1);
    clear_cc(line, x);
    line->chars[x - 1].chr = ' ';
    line->chars[x] = line->chars[x - 1];
  }
}


#ifdef use_display_scrolling

/*
   Scroll the actual display (window contents and its cache).
 */
static int dispscroll_top, dispscroll_bot, dispscroll_lines = 0;

static void
disp_scroll(int topscroll, int botscroll, int scrolllines)
{
  if (dispscroll_lines) {
    dispscroll_lines += scrolllines;
    dispscroll_top = (dispscroll_top + topscroll) / 2;
    dispscroll_bot = (dispscroll_bot + botscroll) / 2;
  }
  else {
    dispscroll_top = topscroll;
    dispscroll_bot = botscroll;
    dispscroll_lines = scrolllines;
  }
}

/*
   Perform actual display scrolling.
   Invoke window scrolling and if successful, adjust display cache.
 */
static void
disp_do_scroll(int topscroll, int botscroll, int scrolllines)
{
  if (!win_do_scroll(topscroll, botscroll, scrolllines))
    return;

  // update display cache
  bool down = scrolllines < 0;
  int lines = abs(scrolllines);
  termline * recycled[lines];
  if (down) {
    for (int l = 0; l < lines; l++) {
      recycled[l] = term.displines[botscroll - 1 - l];
      clearline(recycled[l]);
      for (int j = 0; j < term.cols; j++)
        recycled[l]->chars[j].attr.attr |= ATTR_INVALID;
    }
    for (int l = botscroll - 1; l >= topscroll + lines; l--) {
      term.displines[l] = term.displines[l - lines];
    }
    for (int l = 0; l < lines; l++) {
      term.displines[topscroll + l] = recycled[l];
    }
  }
  else {
    for (int l = 0; l < lines; l++) {
      recycled[l] = term.displines[topscroll + l];
      clearline(recycled[l]);
      for (int j = 0; j < term.cols; j++)
        recycled[l]->chars[j].attr.attr |= ATTR_INVALID;
    }
    for (int l = topscroll; l < botscroll - lines; l++) {
      term.displines[l] = term.displines[l + lines];
    }
    for (int l = 0; l < lines; l++) {
      term.displines[botscroll - 1 - l] = recycled[l];
    }
  }
}

#endif

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is true if the scrolling is permitted to
 * affect the scrollback buffer.
 */
void
term_do_scroll(int topline, int botline, int lines, bool sb)
{
  if (term.hovering) {
    term.hovering = false;
    win_update(true);
  }

#ifdef use_display_scrolling
  int scrolllines = lines;
#endif

  markpos_valid = false;
  assert(botline >= topline && lines != 0);

  bool down = lines < 0; // Scrolling downwards?
  lines = abs(lines);    // Number of lines to scroll by

  lines_scrolled += lines;

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

#ifdef use_display_scrolling
  // Screen scrolling
  int topscroll = topline - term.disptop;
  if (topscroll < term.rows) {
    int botscroll = min(botline - term.disptop, term.rows);
    if (!down && term.disptop && !topline)
      ; // ignore bottom forward scroll if scrolled back
    else
      disp_scroll(topscroll, botscroll, scrolllines);
  }
#endif

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

    term.virtuallines += lines;

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


#define inclpos(p, size) ((p).x == size ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0))

/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
void
term_erase(bool selective, bool line_only, bool from_begin, bool to_end)
{
  term_cursor * curs = &term.curs;
  pos start, end;

  // avoid clearing a "pending wrap" position, where the cursor is 
  // held back on the previous character if it's the last of the line
  if (curs->wrapnext) {
#if 0
    if (!from_begin && to_end)
      return;  // simple approach
#else
    static term_cursor c;
    c = term.curs;
    incpos(c);
    curs = &c;
#endif
  }

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
      int cols = min(line->cols, line->size);
      if (start.x == cols) {
        if (line_only)
          line->lattr &= ~(LATTR_WRAPPED | LATTR_WRAPPED2);
        else
          line->lattr = LATTR_NORM | (line->lattr & LATTR_BIDIMASK);
      }
      else if (!selective ||
               !(line->chars[start.x].attr.attr & ATTR_PROTECTED)
              )
      {
        line->chars[start.x] = term.erase_char;
        line->chars[start.x].attr.attr |= TATTR_CLEAR;
        if (!start.x)
          clear_cc(line, -1);
      }
      if (inclpos(start, cols) && start.y < term.rows)
        line = term.lines[start.y];
    }
  }
}


#define EM_pres 1
#define EM_pict 2
#define EM_text 4
#define EM_emoj 8
#define EM_base 16

struct emoji_base {
  void * res;  // filename (char*/wchar*) or cached image
  uint tags: 11;
  xchar ch: 21;
} __attribute__((packed));

struct emoji_base emoji_bases[] = {
#include "emojibase.t"
};

static int
emoji_idx(xchar ch)
{
  // binary search in table
  int min = 0;
  int max = lengthof(emoji_bases);
  int mid;
  while (max >= min) {
    mid = (min + max) / 2;
    if (emoji_bases[mid].ch < ch) {
      min = mid + 1;
    } else if (emoji_bases[mid].ch > ch) {
      max = mid - 1;
    } else {
      return mid;
    }
  }
  return -1;
}

static uint
emoji_tags(int i)
{
  if (i >= 0)
    return emoji_bases[i].tags;
  else
    return 0;
}

#define echar16

#ifdef echar16
// emoji component encoding to wchar to save half the table size
#define echar wchar
#define ee(x) x >= 0xE0000 ? (wchar)((x & 0xFFF) + 0x6000): x >= 0x1F000 ? (wchar)((x & 0xFFF) + 0x5000) : x
#define ed(x) ((x >> 12) == 6 ? (xchar)x + (0xE0000 - 0x6000) : (x >> 12) == 5 ? (xchar)x + (0x1F000 - 0x5000) : x)
#else
#define echar xchar
#define ee(x) x
#define ed(x) x
#endif

struct emoji_seq {
  void * res;   // filename (char*/wchar*) or cached image
  echar chs[8]; // code points
  char * name;  // short name in emoji-sequences.txt, emoji-zwj-sequences.txt
};

struct emoji_seq emoji_seqs[] = {
// Note that shorter sequences are expected to be sorted behind longer ones!
#include "emojiseqs.t"
};

struct emoji {
  int len: 7;   // emoji width in character cells (== # termchar positions)
  bool seq: 1;  // true: from emoji_seq, false: from emoji_base
  int idx: 24;  // index in either emoji_seq or emoji_base
} __attribute__((packed));

#define dont_debug_emojis 1

/*
   Get emoji sequence "short name".
 */
char *
get_emoji_description(termchar * cpoi)
{
  //struct emoji e = (struct emoji) cpoi->attr.truefg;
  struct emoji * ee = (void *)&cpoi->attr.truefg;

  if (ee->seq) {
    char * en = strdup("");
    for (uint i = 0; i < lengthof(emoji_seqs->chs) && ed(emoji_seqs[ee->idx].chs[i]); i++) {
      xchar xc = ed(emoji_seqs[ee->idx].chs[i]);
      char ec[8];
      sprintf(ec, "U+%04X", xc);
      strappend(en, ec);
      strappend(en, " ");
    }
    strappend(en, "| Emoji sequence: ");
    strappend(en, emoji_seqs[ee->idx].name);
    return en;
  }
  else
    return 0;
}

/*
   Derive file name and path name from emoji sequence; store it.
 */
static bool
check_emoji(struct emoji e)
{
  wchar * * efnpoi;
  if (e.seq) {
    efnpoi = (wchar * *)&emoji_seqs[e.idx].res;
  }
  else {
    efnpoi = (wchar * *)&emoji_bases[e.idx].res;
  }
  if (*efnpoi) { // emoji resource was checked before
    return **efnpoi;  // ... successfully?
  }

  char style = cfg.emojis;
fallback:;

 /*
    File name patterns:
    EmojiOne: 0023-20e3.png
    Noto Emoji: emoji_u0023_20e3.png
  */
  char * pre;
  char * fmt = "%04x";
  char * sep = "-";
  char * suf = ".png";
  bool zwj = true;
  bool sel = true;
  switch (style) {
    when EMOJIS_NOTO:
      pre = "noto/emoji_u";
      sep = "_";
      zwj = true;
      sel = false;
    when EMOJIS_ONE:
      pre = "emojione/";
      sep = "-";
      zwj = false;
      sel = false;
    when EMOJIS_APPLE:
      pre = "apple/";
    when EMOJIS_GOOGLE:
      pre = "google/";
    when EMOJIS_TWITTER:
      pre = "twitter/";
    when EMOJIS_FB:
      pre = "facebook/";
    when EMOJIS_SAMSUNG:
      pre = "samsung/";
    when EMOJIS_WINDOWS:
      pre = "windows/";
    when EMOJIS_NONE:
      pre = "common/";
    otherwise:
      return false;
  }
  char * en = strdup(pre);
  char ec[7];
  if (e.seq) {
    for (uint i = 0; i < lengthof(emoji_seqs->chs) && ed(emoji_seqs[e.idx].chs[i]); i++) {
      xchar xc = ed(emoji_seqs[e.idx].chs[i]);
      if ((xc != 0xFE0F || sel) && (xc != 0x200D || zwj)) {
        if (i)
          strappend(en, sep);
        sprintf(ec, fmt, xc);
        strappend(en, ec);
      }
    }
  }
  else {
    snprintf(ec, 7, "%04x", emoji_bases[e.idx].ch);
    strappend(en, ec);
  }
  strappend(en, suf);
  wchar * wen = cs__utftowcs(en);

  char * ef = get_resource_file(W("emojis"), wen, false);
#ifdef debug_emojis
  printf("emoji <%s> file <%s>\n", en, ef);
#endif
  free(wen);
  free(en);

  if (ef) {
    * efnpoi = path_posix_to_win_w(ef);
    free(ef);
    return true;
  }
  else {
    // if no emoji graphics found, fallback to "common" emojis
    if (style) {
      style = 0;
      goto fallback;
    }

    * efnpoi = W("");  // indicate "checked but not found"
    return false;
  }
}

static int
match_emoji_seq(termchar * d, int maxlen, echar * chs)
{
  int l_text = 0; // number of matched text base character positions
  termchar * basechar = d;
  termchar * curchar = d;

  for (uint i = 0; i < lengthof(emoji_seqs->chs) && ed(chs[i]); i++) {
    if (!curchar)
      return 0;
    if (curchar == basechar)
      l_text++;
    xchar chtxt = curchar->chr;
    if (is_high_surrogate(chtxt)) {
      if (curchar->cc_next) {
        curchar += curchar->cc_next;
        if (is_low_surrogate(curchar->chr))
          chtxt = combine_surrogates(chtxt, curchar->chr);
        else
          return 0;
      }
      else
        return 0;
    }
    if (ed(chs[i]) != chtxt)
      return 0;

    // next text char
    if (curchar->cc_next)
      curchar += curchar->cc_next;
    else if (maxlen > 1) {
      basechar++;
      curchar = basechar;
      maxlen--;
      if (curchar->chr == UCSWIDE && maxlen > 1) {
        l_text++;
        basechar++;
        curchar = basechar;
        maxlen--;
      }
    }
    else
      curchar = 0;
  }
  if (curchar && curchar != basechar)
    return 0;

  return l_text;
}

static struct emoji
match_emoji(termchar * d, int maxlen)
{
  struct emoji emoji;
  emoji.len = 0;

  xchar ch = d->chr;
  termchar * comb = d;
  if (is_high_surrogate(ch) && d->cc_next) {
    comb = d + d->cc_next;
    if (is_low_surrogate(comb->chr))
      ch = combine_surrogates(ch, comb->chr);
  }
  if (comb->cc_next)
    comb += comb->cc_next;
  else
    comb = 0;
  int tagi = emoji_idx(ch);
  uint tags = emoji_tags(tagi);
  if (tags) {
#if defined(debug_emojis) && debug_emojis > 2
    printf("%04X%s%s%s%s%s\n", ch,
           tags & EM_pres ? " pres" : "",
           tags & EM_pict ? " pict" : "",
           tags & EM_text ? " text" : "",
           tags & EM_emoj ? " emoj" : "",
           tags & EM_base ? " base" : ""
          );
#endif
    /* handle the following patterns:
	#basechars (not counting combining marks)
		emoji sequence
			handling
	listed in emoji_seqs (indicated by EM_base):
	1	1F3F4 BLA E007F	tag seq
	2	...		zwj seq, e.g. 1F3F4 200D 2620 FE0F Pirate Flag
	3,4	...		zwj seq: square format → width 3/4
	1	N FE0F 20E3	keycap seq
	2	X X		flag seq / modifier seq
	handled separately:
	1	X FE0E		variation seq: strip; normal display
	1	X FE0F		variation seq: emoji display
	1	X [Emoji_Presentation]	emoji display
	1	X [Extended_Pictographic]	if not in variation seq
	1	X [Emoji]	ignore / normal display; not listed in tables
     */
    struct emoji longest = {0, 0, 0};
    bool foundseq = false;
    if (tags & EM_base) {
      for (uint i = 0; i < lengthof(emoji_seqs); i++) {
        int len = match_emoji_seq(d, maxlen, emoji_seqs[i].chs);
        if (len) {
#if defined(debug_emojis) && debug_emojis > 1
          printf("match");
          for (uint k = 0; k < lengthof(emoji_seqs->chs) && ed(emoji_seqs[i].chs[k]); k++)
            printf(" %04X", ed(emoji_seqs[i].chs[k]));
          printf("\n");
#endif
          emoji.seq = true;
          emoji.idx = i;
          emoji.len = len;
          // match_full_seq: found a match => use it
          // ¬match_full_seq: if there is no graphics, continue 
          // matching for partial prefixes; note this does not work for 
          // ZWJ sequences as the combining ZWJ will prevent a shorter match
          bool match_full_seq = false;
          if (match_full_seq || check_emoji(emoji))
            break;
          else {
            // found a match but there is no emoji graphics for it
            // remember longest match in case we don't find another
            if (!foundseq) {
              longest = emoji;
              foundseq = true;
            }
            // invalidate this match, continue matching
            emoji.len = 0;
          }
        }
      }
    }
    if (!emoji.len) {
      wchar combchr = 0;
      if (comb) {
        if (comb->cc_next)
          combchr = -1;
        else
          combchr = comb->chr;
      }
      emoji.seq = false;
      emoji.idx = tagi;
      emoji.len = maxlen > 1 && d[1].chr == UCSWIDE ? 2 : 1;
      /*
	remaining, non-sequence patterns:
	1	X FE0E		variation seq: strip; normal display
	1	X FE0F		variation seq: emoji display
	1	X [Emoji_Presentation]	emoji display
	1	X [Extended_Pictographic]	if not a variation (none)
       */
      if ((tags & EM_text) && combchr == 0xFE0E) {
        // VARIATION SELECTOR-15: display text style
        emoji.len = 0;
      }
      else if ((tags & EM_emoj) && combchr == 0xFE0F) {
        // VARIATION SELECTOR-16: display emoji style
      }
      else if (combchr) {
        emoji.len = 0;  // suppress emoji style with combining
      }
      else if (tags & EM_pres) {
        // display presentation
      }
      else if ((tags & (EM_emoj | EM_pres | EM_pict)) && (d->attr.attr & ATTR_FRAMED)) {
        // with explicit attribute, display pictographic
      }
#ifdef support_only_pictographics
      else if ((tags & EM_pict) && !(tags & (EM_text | EM_emoj))) {
        // we could support this group to display pictographic,
        // however, there are no emoji graphics for them anyway, so:
        emoji.len = 0;
      }
#endif
#ifdef support_other_pictographics
      else if ((tags & EM_pict) && (tags & (EM_text | EM_emoj))) {
        // we could support this group to display pictographic,
        // however, Unicode specifies them for explicit variations,
        // so let's default to text style
        emoji.len = 0;
      }
#endif
      else
        emoji.len = 0;  // display text style
    }
    if (!emoji.len) {
      // not found another match; if we had a "longest match" before, 
      // but continued to search because it had no graphics, let's use it
      if (foundseq)
        return longest;
    }
  }

  return emoji;
}

static void
emoji_show(int x, int y, struct emoji e, int elen, cattr eattr, ushort lattr)
{
  (void)eattr;
  wchar * efn;
  if (e.seq) {
    efn = emoji_seqs[e.idx].res;
  }
  else {
    efn = emoji_bases[e.idx].res;
  }
#ifdef debug_emojis
  printf("emoji_show <%ls>\n", efn);
#endif
  if (efn && *efn)
    win_emoji_show(x, y, efn, elen, lattr);
}

void
term_paint(void)
{
#ifdef use_display_scrolling
  if (dispscroll_lines) {
    disp_do_scroll(dispscroll_top, dispscroll_bot, dispscroll_lines);
    dispscroll_lines = 0;
  }
#endif

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

    // Prevent nested emoji sequence matching from matching partial subseqs
    int emoji_col = 0;  // column from which to match for emoji sequences

   /*
    * First loop: work along the line deciding what we want
    * each character cell to look like.
    */
    for (int j = 0; j < term.cols; j++) {
      termchar *d = chars + j;
      scrpos.x = backward ? backward[j] : j;
      wchar tchar = d->chr;
      cattr tattr = d->attr;

     /* Many Windows fonts don't have the Unicode hyphen, but groff
      * uses it for man pages, so display it as the ASCII version.
      */
      if (tchar == 0x2010)
        tchar = '-';

      if (j < term.cols - 1 && d[1].chr == UCSWIDE)
        tattr.attr |= ATTR_WIDE;

     /* Video reversing things */
      bool selected =
        term.selected &&
        ( term.sel_rect
          ? posPle(term.sel_start, scrpos) && posPlt(scrpos, term.sel_end)
          : posle(term.sel_start, scrpos) && poslt(scrpos, term.sel_end)
        );

      if (selected) {
        tattr.attr |= TATTR_SELECTED;

        colour bg = win_get_colour(SEL_COLOUR_I);
        if (bg != (colour)-1) {
          tattr.truebg = bg;
          tattr.attr = (tattr.attr & ~ATTR_BGMASK) | (TRUE_COLOUR << ATTR_BGSHIFT);

          colour fg = win_get_colour(SEL_TEXT_COLOUR_I);
          if (fg == (colour)-1)
            fg = apply_attr_colour(tattr, ACM_SIMPLE).truefg;
          static uint mindist = 22222;
          bool too_close = colour_dist(fg, tattr.truebg) < mindist;
          if (too_close)
            fg = brighten(fg, tattr.truebg, false);
          tattr.truefg = fg;
          tattr.attr = (tattr.attr & ~ATTR_FGMASK) | (TRUE_COLOUR << ATTR_FGSHIFT);
        }
        else
          tattr.attr ^= ATTR_REVERSE;
      }

      if (term.hovering &&
          (term.hoverlink >= 0
           ? term.hoverlink == tattr.link
           : posle(term.hover_start, scrpos) && poslt(scrpos, term.hover_end)
          )
         )
      {
        tattr.attr &= ~UNDER_MASK;
        tattr.attr |= ATTR_UNDER;
        if (cfg.hover_colour != (colour)-1) {
          tattr.attr |= ATTR_ULCOLOUR;
          tattr.ulcolr = cfg.hover_colour;
        }
      }

      bool flashchar = term.in_vbell &&
                       ((cfg.bell_flash_style & FLASH_FULL)
                        ||
                        ((cfg.bell_flash_style & FLASH_BORDER)
                         && (i == 0 || j == 0 ||
                             i == term.rows - 1 || j == term.cols - 1
                            )
                        )
                       );

      if (flashchar) {
        if (cfg.bell_flash_style & FLASH_REVERSE)
          tattr.attr ^= ATTR_REVERSE;
        else {
          tattr.truebg = apply_attr_colour(tattr, ACM_VBELL_BG).truebg;
          tattr.attr = (tattr.attr & ~ATTR_BGMASK) | (TRUE_COLOUR << ATTR_BGSHIFT);
        }
      }

      int match = in_results(scrpos);
      if (match > 0) {
        tattr.attr |= TATTR_RESULT;
        if (match > 1) {
          tattr.attr |= TATTR_CURRESULT;
        }
      } else {
        tattr.attr &= ~TATTR_RESULT;
      }
      if (markpos_valid && (displine->lattr & (LATTR_MARKED | LATTR_UNMARKED))) {
        tattr.attr |= TATTR_MARKED;
        if (scrpos.y == markpos)
          tattr.attr |= TATTR_CURMARKED;
      } else {
        tattr.attr &= ~TATTR_MARKED;
      }

     /* 'Real' blinking ? */
      if (term.blink_is_real && (tattr.attr & ATTR_BLINK)) {
        if (term.has_focus && term.tblinker)
          tchar = ' ';
        tattr.attr &= ~ATTR_BLINK;
      }
      if (term.blink_is_real && (tattr.attr & ATTR_BLINK2)) {
        if (term.has_focus && term.tblinker2)
          tchar = ' ';
        tattr.attr &= ~ATTR_BLINK2;
      }

     /* Match emoji sequences
      * and replace by emoji indicators
      */
      if (cfg.emojis && j >= emoji_col) {
        struct emoji e;
        if ((tattr.attr & TATTR_EMOJI) && !(tattr.attr & ATTR_FGMASK)) {
          // previously marked subsequent emoji sequence component
          e.len = 0;
        }
        else
          e = match_emoji(d, term.cols - j);
        if (e.len) {  // we have matched an emoji (sequence)
          // avoid subsequent matching of a partial emoji subsequence
          emoji_col = j + e.len;

          // check whether emoji graphics exist for the emoji
          bool ok = check_emoji(e);

          // check whether all emoji components have the same attributes
          bool equalattrs = true;
          for (int i = 1; i < e.len && equalattrs; i++) {
# define IGNATTR (ATTR_WIDE | ATTR_FGMASK | TATTR_COMBINING)
            if ((d[i].attr.attr & ~IGNATTR) != (d->attr.attr & ~IGNATTR)
               || d[i].attr.truebg != d->attr.truebg
               )
              equalattrs = false;
          }
#ifdef debug_emojis
          printf("matched len %d seq %d idx %d ok %d atr %d\n", e.len, e.seq, e.idx, ok, equalattrs);
#endif

          // modify character data to trigger later emoji display
          if (ok && equalattrs) {
            d->attr.attr &= ~ATTR_FGMASK;
            d->attr.attr |= TATTR_EMOJI | e.len;

            //d->attr.truefg = (uint)e;
            struct emoji * ee = &e;
            uint em = *(uint *)ee;
            d->attr.truefg = em;

            // refresh cached copy to avoid display delay
            if (tattr.attr & TATTR_SELECTED) {
              tattr = d->attr;
              // need to propagate this to enable emoji highlighting
              tattr.attr |= TATTR_SELECTED;
            }
            else
              tattr = d->attr;
            // inhibit rendering of subsequent emoji sequence components
            for (int i = 1; i < e.len; i++) {
              d[i].attr.attr &= ~ATTR_FGMASK;
              d[i].attr.attr |= TATTR_EMOJI;
              d[i].attr.truefg = em;
            }
          }
        }
      }

     /* Mark box drawing, block and some other characters 
      * that should connect to their neighbour cells and thus 
      * be zoomed to the actual cell size including spacing (padding);
      * also, for those, an italic attribute shall be ignored
      */
      if (tchar >= 0x2320 &&
          ((tchar >= 0x2500 && tchar <= 0x259F)
           || (tchar >= 0x239B && tchar <= 0x23B3)
           || (tchar >= 0x23B7 && tchar <= 0x23BD)
           || wcschr(W("〳〴〵⌠⌡⏐"), tchar)
          )
         )
      {
        tattr.attr |= TATTR_ZOOMFULL;
        tattr.attr &= ~ATTR_ITALIC;
      }

     /*
      * Check the font we'll _probably_ be using to see if
      * the character is wide when we don't want it to be.
      */
      if (tchar >= 0xE000 && tchar < 0xF900) {
        // don't tamper with width of Private Use characters
      }
      else if (tchar != dispchars[j].chr ||
          tattr.attr != (dispchars[j].attr.attr & ~(ATTR_NARROW | DATTR_MASK))
              )
      {
        if ((tattr.attr & ATTR_WIDE) == 0 && win_char_width(tchar) == 2
            // do not tamper with graphics
            && !line->lattr
            // and restrict narrowing to ambiguous width chars
            //&& ambigwide(tchar)
            // but then they will be clipped...
           ) {
          tattr.attr |= ATTR_NARROW;
        }
        else if (tattr.attr & ATTR_WIDE
                 // guard character expanding properly to avoid 
                 // false hits as reported for CJK in #570,
                 // considering that Windows may report width 1 
                 // for double-width characters 
                 // (if double-width by font substitution)
                 && cs_ambig_wide && !font_ambig_wide
                 && win_char_width(tchar) == 1 // && !widerange(tchar)
                 // and reassure to apply this only to ambiguous width chars
                 && ambigwide(tchar)
                ) {
          tattr.attr |= ATTR_EXPAND;
        }
      }
      else if (dispchars[j].attr.attr & ATTR_NARROW)
        tattr.attr |= ATTR_NARROW;

#define dont_debug_width_scaling
#ifdef debug_width_scaling
      if (tattr.attr & (ATTR_EXPAND | ATTR_NARROW | ATTR_WIDE))
        printf("%04X w %d enw %02X\n", tchar, win_char_width(tchar), (uint)(((tattr.attr & (ATTR_EXPAND | ATTR_NARROW | ATTR_WIDE)) >> 24)));
#endif

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
      newchars[curs_x].attr.attr |=
        (!term.has_focus ? TATTR_PASCURS :
         term.cblinker || !term_cursor_blinks() ? TATTR_ACTCURS : 0) |
        (term.curs.wrapnext ? TATTR_RIGHTCURS : 0);

      if (term.cursor_invalid)
        dispchars[curs_x].attr.attr |= ATTR_INVALID;

      // try to fix #612 "cursor isn’t hidden right away"
      if (newchars[curs_x].attr.attr != dispchars[curs_x].attr.attr)
        dispchars[curs_x].attr.attr |= ATTR_INVALID;
    }

   /*
    * Now loop over the line again, noting where things have changed.
    *
    * During this loop, we keep track of where we last saw DATTR_STARTRUN.
    * Any mismatch automatically invalidates
    * _all_ of the containing run that was last printed: that is,
    * any rectangle that was drawn in one go in the
    * previous update should be either left completely alone
    * or overwritten in its entirety. This, along with the
    * expectation that front ends clip all text runs to their
    * bounding rectangle, should solve any possible problems
    * with fonts that overflow their character cells.
    *
    * For the new italic overhang feature, this had to be extended 
    * (firstitalicstart) as italic chunks are painted in reverse order.
    * Also, to clear overhang artefacts after scrolling, a run to the 
    * right of an italic run needs to be repainted (prevdirtyitalic).
    * Also, after the loop, overhang into the right padding border 
    * must be detected and propagated.
    * And this in a way that does not cause continuous repainting 
    * of further unchanged cells...
    */
    int laststart = 0;
    int firstitalicstart = -1;
    bool prevdirtyitalic = false;
    bool firstdirtyitalic = false;
    bool dirtyrect = false;
    for (int j = 0; j < term.cols; j++) {
      if (dispchars[j].attr.attr & DATTR_STARTRUN) {
        laststart = j;
        dirtyrect = false;
        if (firstitalicstart < 0 && newchars[j].attr.attr & ATTR_ITALIC)
          firstitalicstart = j;
      }

      if (!dirtyrect  // test this first for potential speed-up
          && (dispchars[j].chr != newchars[j].chr
              || (dispchars[j].attr.truefg != newchars[j].attr.truefg)
              || (dispchars[j].attr.truebg != newchars[j].attr.truebg)
              || (dispchars[j].attr.ulcolr != newchars[j].attr.ulcolr)
              || (dispchars[j].attr.attr & ~DATTR_STARTRUN) != newchars[j].attr.attr
              || (prevdirtyitalic && (dispchars[j].attr.attr & DATTR_STARTRUN))
             ))
      {
        int start = firstitalicstart >= 0 ? firstitalicstart : laststart;
        firstitalicstart = -1;
        for (int k = start; k < j; k++)
          dispchars[k].attr.attr |= ATTR_INVALID;
        dirtyrect = true;
        prevdirtyitalic = false;
      }
      if (dirtyrect && dispchars[j].attr.attr & ATTR_ITALIC)
        prevdirtyitalic = true;
      else if (dispchars[j].attr.attr & DATTR_STARTRUN)
        prevdirtyitalic = false;
      if (j == 0)
        firstdirtyitalic = prevdirtyitalic;

      if (dirtyrect)
        dispchars[j].attr.attr |= ATTR_INVALID;
    }
    if (prevdirtyitalic) {
      // clear overhang into right padding border
      win_text(term.cols, i, W(" "), 1, CATTR_DEFAULT, (cattr*)&CATTR_DEFAULT, line->lattr | LATTR_CLEARPAD, false);
    }
    if (firstdirtyitalic) {
      // clear overhang into left padding border
      win_text(-1, i, W(" "), 1, CATTR_DEFAULT, (cattr*)&CATTR_DEFAULT, line->lattr | LATTR_CLEARPAD, false);
    }

   /*
    * Finally, loop once more and actually do the drawing.
    */
    // control line overlay; as opposed to character overlay implemented 
    // by the "pending overlay" buffer below, this works by repeating 
    // the last line loop and only drawing the overlay characters this time
    bool overlaying = false;
    overlay:;
    bool do_overlay = false;

    int maxtextlen = max(term.cols, 16);
    wchar text[maxtextlen];
    cattr textattr[maxtextlen];
    int textlen = 0;

    bool has_rtl = false;
    uchar bc = 0;
    bool dirty_run = (line->lattr != displine->lattr);
    bool dirty_line = dirty_run;
    cattr attr = CATTR_DEFAULT;
    int start = 0;

    displine->lattr = line->lattr;

    // buffer for pending overlay output; for support of character overhang
    // (italics and wide glyphs), such chunks are output in two steps;
    // the first output paints the background (and possibly manual underline)
    // and the second output paints the text, over the adjacent chunks
    wchar ovl_text[maxtextlen];
    cattr ovl_textattr[maxtextlen];
    int ovl_len = 0;
    int ovl_x, ovl_y;
    cattr ovl_attr;
    ushort ovl_lattr;
    bool ovl_has_rtl;

    void flush_text()
    {
      if (ovl_len) {
        win_text(ovl_x, ovl_y, ovl_text, ovl_len, ovl_attr, ovl_textattr, ovl_lattr | LATTR_DISP2, ovl_has_rtl);
        ovl_len = 0;
      }
    }

#define dont_debug_run

#define dont_debug_out_text

#ifdef debug_run
# define debug_out_text
#endif

    void out_text(int x, int y, wchar *text, int len, cattr attr, cattr *textattr, ushort lattr, bool has_rtl)
    {
#ifdef debug_out_text
      wchar t[len + 1]; wcsncpy(t, text, len); t[len] = 0;
      for (int i = len - 1; i >= 0 && t[i] == ' '; i--)
        t[i] = 0;
      if (*t) {
        printf("out <%ls>\n", t);
        for (int i = 0; i < len; i++)
          printf(" %04X", t[i]);
        printf("\n");
      }
#endif
      if (attr.attr & TATTR_EMOJI) {
        int elen = attr.attr & ATTR_FGMASK;
        cattr eattr = attr;
        eattr.attr &= ~(ATTR_WIDE | TATTR_COMBINING);
        wchar esp[] = W("        ");
        if (elen) {
          if (!overlaying) {
            if (newchars[x].attr.attr & TATTR_SELECTED) {
              // here we handle background colour once more because
              // somehow the selection highlighting information from above
              // got lost in the chaos of chars[], newchars[], attr, tattr...
              // some substantial revision might be good here, in theory...

              // the main problem here is the reuse of truefg as an 
              // emoji indicator; we have to make sure truefg isn't 
              // used anymore for an emoji...
              eattr.attr |= TATTR_SELECTED;
              colour bg = eattr.attr & ATTR_REVERSE
                          ? win_get_colour(SEL_TEXT_COLOUR_I)
                          : win_get_colour(SEL_COLOUR_I);
              if (bg == (colour)-1)
                bg = eattr.attr & ATTR_REVERSE
                          ? win_get_colour(BG_COLOUR_I)
                          : win_get_colour(FG_COLOUR_I);
              eattr.truebg = bg;
              eattr.attr = (eattr.attr & ~ATTR_BGMASK) | (TRUE_COLOUR << ATTR_BGSHIFT);
              eattr.attr &= ~ATTR_REVERSE;
            }

            win_text(x, y, esp, elen, eattr, textattr, lattr | LATTR_DISP1, has_rtl);
            flush_text();
          }
#ifdef debug_emojis
          eattr.attr &= ~(ATTR_BGMASK | ATTR_FGMASK);
          eattr.attr |= 6 << ATTR_BGSHIFT | 4;
          esp[0] = '0' + elen;
          win_text(x, y, esp, elen, eattr, textattr, lattr | LATTR_DISP2, has_rtl);
#endif
          if (cfg.emoji_placement == EMPL_FULL && !overlaying)
            do_overlay = true;  // display in overlaying loop
          else {
            //struct emoji e = (struct emoji) eattr.truefg;
            struct emoji * ee = (void *)&eattr.truefg;
            emoji_show(x, y, *ee, elen, eattr, lattr);
          }
        }
#ifdef debug_emojis
        else {
          eattr.attr &= ~(ATTR_BGMASK | ATTR_FGMASK);
          eattr.attr |= 4 << ATTR_BGSHIFT | 6;
          esp[0] = '0';
          win_text(x, y, esp, 1, eattr, textattr, lattr | LATTR_DISP2, has_rtl);
        }
#endif
      }
      else if (overlaying) {
        return;
      }
      else if (attr.attr & (ATTR_ITALIC | TATTR_COMBDOUBL)) {
        win_text(x, y, text, len, attr, textattr, lattr | LATTR_DISP1, has_rtl);
        flush_text();
        ovl_x = x;
        ovl_y = y;
        wcsncpy(ovl_text, text, len);
        ovl_len = len;
        ovl_attr = attr;
        memcpy(ovl_textattr, textattr, len * sizeof(cattr));
        ovl_lattr = lattr;
        ovl_has_rtl = has_rtl;
      }
      else {
        win_text(x, y, text, len, attr, textattr, lattr, has_rtl);
        flush_text();
      }
    }

   /*
    * Third loop, for actual drawing.
    */
    for (int j = 0; j < term.cols; j++) {
      termchar *d = chars + j;
      cattr tattr = newchars[j].attr;
      wchar tchar = newchars[j].chr;
      // Note: newchars[j].cc_next is always 0; use chars[]
      xchar xtchar = tchar;
#ifdef proper_non_BMP_classification
      // this is the correct way to later check for the bidi_class,
      // but let's not touch non-BMP here for now because:
      // - some non-BMP ranges do not align to cell width (e.g. Hieroglyphs)
      // - right-to-left non-BMP does not work (GetCharacterPlacementW fails)
      if (is_high_surrogate(tchar) && chars[j].cc_next) {
        termchar *t1 = &chars[j + chars[j].cc_next];
        if (is_low_surrogate(t1->chr))
          xtchar = combine_surrogates(tchar, t1->chr);
      }
#endif

      if ((dispchars[j].attr.attr ^ tattr.attr) & ATTR_WIDE)
        dirty_line = true;

#ifdef debug_run
#define trace_run(tag)	({/*if (tchar & 0xFF00)*/ if (tchar != ' ') printf("break (%s) (%04X)%04X\n", tag, j > 0 ? newchars[j - 1].chr : 0, tchar);})

#else
#define trace_run(tag)	(void)0
#endif

      bool break_run = (tattr.attr != attr.attr)
                    || (tattr.truefg != attr.truefg)
                    || (tattr.truebg != attr.truebg)
                    || (tattr.ulcolr != attr.ulcolr);

      inline bool has_comb(termchar * tc)
      {
        if (!tc->cc_next)
          return false;
        if (!is_high_surrogate(tc->chr))
          return true;
        tc += tc->cc_next;
        return tc->cc_next;
      }

     /*
      * Break on both sides of any combined-character cell.
      */
      if (has_comb(d) || (j > 0 && has_comb(&d[-1])))
        trace_run("cc"), break_run = true;

#ifdef keep_non_BMP_characters_together_in_one_chunk
      // this was expected to speed up non-BMP display 
      // but the effect is not significant, if any
      // also, this spoils two other issues about non-BMP display:
#warning non-BMP RTL will be in wrong order
#warning some non-BMP ranges (e.g. Egyptian Hieroglyphs) are not cell-adjusted
     /*
      * Break when exceeding output buffer length.
      */
      if (is_high_surrogate(d->chr) && textlen + 2 >= maxtextlen)
        trace_run("max"), break_run = true;
     /*
      * Break when switching BMP/non-BMP.
      */
      if (j > 0 && is_high_surrogate(d->chr) ^ is_high_surrogate(d[-1].chr))
        trace_run("bmp"), break_run = true;
#else
     /*
      * Break on both sides of non-BMP character.
      */
      if (j > 0 && (is_high_surrogate(d->chr) || is_high_surrogate(d[-1].chr)))
        trace_run("bmp"), break_run = true;
#endif

      if (!dirty_line) {
        if (dispchars[j].chr == tchar &&
            (dispchars[j].attr.attr & ~DATTR_STARTRUN) == tattr.attr)
          trace_run("str"), break_run = true;
        else if (!dirty_run && textlen == 1)
          trace_run("len"), break_run = true;
      }

      uchar tbc = bidi_class(xtchar);

      if (textlen && tbc != bc) {
        if (!is_sep_class(tbc) && !is_sep_class(bc))
          // break at RTL and other changes to avoid glyph confusion (#285)
          trace_run("bcs"), break_run = true;
        //else if (is_punct_class(tbc) || is_punct_class(bc))
        else if ((tbc == EN) ^ (bc == EN))
          // break at digit to avoid adaptation to script style
          trace_run("bcp"), break_run = true;
      }
      bc = tbc;

      if (break_run) {
        if ((dirty_run && textlen) || overlaying)
          out_text(start, i, text, textlen, attr, textattr, line->lattr, has_rtl);
        start = j;
        textlen = 0;
        has_rtl = false;
        attr = tattr;
        dirty_run = dirty_line;
      }

      bool do_copy =
        !termchars_equal_override(&dispchars[j], d, tchar, tattr);
      dirty_run |= do_copy;

      if (tchar == SIXELCH) {
        // displaying region of a SIXEL image before actual graphics display;
        // this includes selection over a SIXEL image but also moments 
        // before display of a new image or refreshed image when scrolling 
        // or otherwise refreshing the screen.
        // options:
        // indicate visually that the image will not be copied
        // (would flicker in other cases)
        //text[textlen] = 0xFFFD;
        // or:
        // blank region of SIXEL image
        text[textlen] = ' ';  // or any of █░▒▓▚▞, e.g. 0x2591 ?
      }
      else
        text[textlen] = tchar;
      ///textattr[textlen] = tattr;
      textlen++;

      if (!has_rtl)
        has_rtl = is_rtl_class(tbc);

#define dont_debug_surrogates

      if (d->cc_next) {
        termchar *dd = d;
        while (dd->cc_next && textlen < maxtextlen) {
#ifdef debug_surrogates
          wchar prev = dd->chr;
#endif
          dd += dd->cc_next;

          // mark combining unless pseudo-combining surrogates
          if (!is_low_surrogate(dd->chr)) {
            if (tattr.attr & TATTR_EMOJI)
              break;
            attr.attr |= TATTR_COMBINING;
          }
          if (combiningdouble(dd->chr))
            attr.attr |= TATTR_COMBDOUBL;

          textattr[textlen] = dd->attr;
          if (cfg.emojis && dd->chr == 0xFE0E)
            ; // skip text style variation selector
          else if (dd->chr >= 0x2066 && dd->chr <= 0x2069)
            // hide bidi isolate mark glyphs (if handled zero-width)
            text[textlen++] = 0x200B;  // zero width space
          else
            text[textlen++] = dd->chr;
#ifdef debug_surrogates
          ucschar comb = 0xFFFFF;
          if ((prev & 0xFC00) == 0xD800 && (dd->chr & 0xFC00) == 0xDC00)
            comb = ((ucschar) (prev - 0xD7C0) << 10) | (dd->chr & 0x03FF);
          printf("comb (%04X) %04X %04X (%05X) %11llX\n", 
                 d->chr, prev, dd->chr, comb, attr.attr);
#endif
        }
      }

      if (do_copy) {
        copy_termchar(displine, j, d);  // may change displine->chars
        dispchars = displine->chars;
        dispchars[j].chr = tchar;
        dispchars[j].attr = tattr;
        if (start == j)
          dispchars[j].attr.attr |= DATTR_STARTRUN;
      }

     /* If it's a wide char step along to the next one. */
      if ((tattr.attr & ATTR_WIDE) && ++j < term.cols) {
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
      out_text(start, i, text, textlen, attr, textattr, line->lattr, has_rtl);
    if (!overlaying)
      flush_text();

   /*
    * Draw any pending overlay characters in one more loop.
    */
    if (do_overlay && !overlaying) {
      overlaying = true;
      goto overlay;
    }

   /*
    * Release the line data fetched from the screen or scrollback buffer.
    */
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
    if ((term.displines[i]->lattr & LATTR_MODE) == LATTR_NORM)
      for (int j = left; j <= right && j < term.cols; j++)
        term.displines[i]->chars[j].attr.attr |= ATTR_INVALID;
    else
      for (int j = left / 2; j <= right / 2 + 1 && j < term.cols; j++)
        term.displines[i]->chars[j].attr.attr |= ATTR_INVALID;
  }
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 * The first parameter may also be SB_PRIOR or SB_NEXT, to scroll to 
 * the prior or next distinguished/marked position (to be searched).
 */
void
term_scroll(int rel, int where)
{
  if (term.hovering) {
    term.hovering = false;
    win_update(true);
  }

  int sbtop = -sblines();
  int sbbot = term_last_nonempty_line();
  bool do_schedule_update = false;

  if (rel == SB_PRIOR || rel == SB_NEXT) {
    if (!markpos_valid) {
      markpos = sbbot;
      markpos_valid = true;
    }
    int y = markpos;
    while ((rel == SB_PRIOR) ? y-- > sbtop : y++ < sbbot) {
      termline * line = fetch_line(y);
      if (line->lattr & LATTR_MARKED) {
        markpos = y;
        term.disptop = y;
        break;
      }
    }
    do_schedule_update = true;
  }
  else
    term.disptop = (rel < 0 ? 0 : rel > 0 ? sbtop : term.disptop) + where;

  if (term.disptop < sbtop)
    term.disptop = sbtop;
  if (term.disptop > 0)
    term.disptop = 0;
  win_update(false);

  if (do_schedule_update) {
    win_schedule_update();
    do_update();
  }
}

void
term_set_focus(bool has_focus, bool may_report)
{
  if (!has_focus)
    term.hovering = false;

  if (has_focus != term.has_focus) {
    term.has_focus = has_focus;
    term_schedule_cblink();
  }

  if (has_focus != term.focus_reported && may_report) {
    term.focus_reported = has_focus;
    if (term.report_focus)
      child_write(has_focus ? "\e[I" : "\e[O", 3);
  }
}

void
term_update_cs(void)
{
  term_cursor *curs = &term.curs;
  cs_set_mode(
    curs->oem_acs ? CSM_OEM :
    curs->utf ? CSM_UTF8 :
    curs->csets[curs->gl] == CSET_OEM ? CSM_OEM : CSM_DEFAULT
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
    win_update(false);
  }
}
