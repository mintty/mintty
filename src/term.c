// term.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2016-2020 Thomas Wolff
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
int
term_cursor_type(void)
{
  return term.cursor_type == -1 ? cfg.cursor_type : term.cursor_type;
}

static bool
term_cursor_blinks(void)
{
  return term.cursor_blinkmode
      || (term.cursor_blinks == -1 ? cfg.cursor_blinks : term.cursor_blinks);
}

void
term_hide_cursor(void)
{
  if (term.cursor_on) {
    term.cursor_on = false;
    win_update(false);
  }
}

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
    win_set_timer(cblink_cb, term.cursor_blink_interval ?: cursor_blink_ticks());
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
  curs->width = 0;
  curs->gl = 0;
  curs->gr = 0;
  curs->oem_acs = 0;
  curs->utf = false;
  for (uint i = 0; i < lengthof(curs->csets); i++)
    curs->csets[i] = CSET_ASCII;
  curs->decsupp = CSET_DECSPGR;
  curs->cset_single = CSET_ASCII;

  curs->bidimode = 0;

  curs->origin = false;
}

static void
term_bell_reset(term_bell *bell)
{
  bell->vol = 8;  // not reset by xterm
  bell->last_vol = bell->vol;
  bell->last_bell = 0;
}

void
term_reset(bool full)
{
  if (term.cmd_buf == NULL) {
    term.cmd_buf = newn(char, 128);
    term.cmd_buf_cap = 128;
  }

  term.state = NORMAL;
  term.vt52_mode = 0;

  // DECSTR attributes and cursor states to be reset
  term_cursor_reset(&term.curs);
  term_cursor_reset(&term.saved_cursors[0]);
  term_cursor_reset(&term.saved_cursors[1]);
  term_update_cs();
  term.erase_char = basic_erase_char;
  // these used to be in term_cursor, thus affected by cursor restore
  term.decnrc_enabled = false;
  term.autowrap = true;
  term.rev_wrap = cfg.old_wrapmodes;

  // DECSTR states to be reset (in addition to cursor states)
  // https://www.vt100.net/docs/vt220-rm/table4-10.html
  term.cursor_on = true;
  term.insert = false;
  term.marg_top = 0;
  term.marg_bot = term.rows - 1;
  term.marg_left = 0;
  term.marg_right = term.cols - 1;
  term.app_cursor_keys = false;
  term.app_scrollbar = false;

  if (full) {
    term.lrmargmode = false;
    term.deccolm_allowed = cfg.enable_deccolm_init;  // not reset by xterm
    term.vt220_keys = vt220(cfg.term);  // not reset by xterm
    term.app_keypad = false;  // xterm only with RIS
    term.app_control = 0;
    term.auto_repeat = cfg.auto_repeat;  // not supported by xterm
    term.repeat_rate = 0;
    term.attr_rect = false;
    term.deccolm_noclear = false;
  }
  term.modify_other_keys = 0;  // xterm resets this

  term.backspace_sends_bs = cfg.backspace_sends_bs;  // xterm only with RIS
  term.delete_sends_del = cfg.delete_sends_del;  // not reset by xterm
  if (full && term.tabs) {
    for (int i = 0; i < term.cols; i++)
      term.tabs[i] = (i % 8 == 0);
  }
  if (full) {
    term.newtab = 1;  // set default tabs on resize
    term.rvideo = 0;  // not reset by xterm
    term_bell_reset(&term.bell);
    term_bell_reset(&term.marginbell);
    term.margin_bell = false;  // not reset by xterm
    term.ring_enabled = false;
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
    term.wheel_reporting_xterm = false;
    term.wheel_reporting = true;
    term.app_wheel = false;
    term.echoing = false;
    term.bracketed_paste = false;
    term.wide_indic = false;
    term.wide_extra = false;
    term.disable_bidi = false;
    term.enable_bold_colour = cfg.bold_as_colour;
    term.enable_blink_colour = true;
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
  term.cursor_size = 0;
  term.cursor_blinks = -1;
  term.cursor_blink_interval = 0;
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
    term.curs.x = 0;
    term.curs.y = 0;
  }

  term.in_vbell = false;
  term_schedule_tblink();
  term_schedule_tblink2();
  term_schedule_cblink();
  term_clear_scrollback();

  term.iso_guarded_area = false;

  term.detect_progress = cfg.progress_bar;
  taskbar_progress(-9);

  term.suspend_update = 0;
  term.no_scroll = 0;
  term.scroll_mode = 0;

  term_schedule_search_update();

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

static int
in_results(pos scrpos)
{
  if (term.results.xquery_length == 0) {
    return 0;
  }
  int idx = scrpos.x + (scrpos.y + term.sblines) * term.cols;
  if (!(term.results.range_begin <= idx && idx < term.results.range_end)) {
    term_search_expand(idx);
  }

  int b = 0;
  int e = term.results.length;
  while (b < e) {
    int m = (b + e) / 2;
    if (term.results.results[m].idx > idx) {
      e = m;
    } else {
      b = m + 1;
    }
  }

  if (e <= 0) {
    return 0;
  }
  result hit = term.results.results[e - 1];
  if (idx >= hit.idx + hit.len) {
    return 0;
  }
  int match = 1;
  if (term.results.current.idx <= idx && idx < term.results.current.idx + term.results.current.len) {
    match += 1;
  }
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
  // speedup ASCII
  if (ch < 0x80) {
    if (ch >= 'A' && ch <= 'Z')
      return ch + 'a' - 'A';
    else
      return ch;
  }

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
    xchar xqueri;
    if ((prev & 0xFC00) == 0xD800 && (needle[i] & 0xFC00) == 0xDC00)
      xqueri = ((xchar) (prev - 0xD7C0) << 10) | (needle[i] & 0x03FF);
    else {
      ++xlen;
      xqueri = needle[i];
    }
    xquery[xlen] = case_fold(xqueri);
    prev = needle[i];
  }
  xquery[++xlen] = 0;

  free(term.results.xquery);
  term.results.xquery = xquery;
  term.results.xquery_length = xlen;
  term.results.update_type = FULL_UPDATE;
}

void
term_update_search(void)
{
  if (term.results.update_type == NO_UPDATE)
    return;
  term.results.update_type = NO_UPDATE;

  if (term.results.xquery_length == 0) {
    term_clear_search();
    return;
  }

  term_clear_results();
  // The actual search happens inside in_results().
}

// return search results contained by [begin, end)
static void
do_search(int begin, int end)
{
  //printf("do_search %d %d\n", begin, end);
  if (term.results.xquery_length == 0) {
    return;
  }

  init_case_folding();

  /* the position of current char */
  int cpos = begin;
  /* the number of matched chars in the current run */
  int npos = 0;
  /* the number of matched cells in the current run (anpos >= npos) */
  int anpos = 0;

  // Loop over every character and search for query.
  termline * line = NULL;
  int line_y = -1;
  while (cpos < end) {
    // Determine the current position.
    int x = (cpos % term.cols);
    int y = (cpos / term.cols);
    if (line_y != y) {
      // If our current position isn't in the termline, add it in.
      if (line) {
        release_line(line);
      }
      line = fetch_line(y - term.sblines);
      line_y = y;
    }

    if (npos == 0 && cpos + term.results.xquery_length >= end) {
      // Not enough data to match.
      break;
    }

    termchar * chr = line->chars + x;
    xchar ch = chr->chr;
    if (is_high_surrogate(chr->chr) && chr->cc_next) {
      termchar * cc = chr + chr->cc_next;
      if (is_low_surrogate(cc->chr)) {
        ch = combine_surrogates(chr->chr, cc->chr);
      }
    }
    xchar pat = term.results.xquery[npos];
    bool match = case_fold(ch) == pat;
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
      result run = {
        .idx = cpos - anpos + 1,
        .len = anpos
      };
      assert(begin <= run.idx && (run.idx + run.len) <= end);
      // Append result
      results_add(run);
      npos = 0;
      anpos = 0;
    }

    ++cpos;
  }

  // Clean up
  if (line) {
      release_line(line);
  }
}

static void
results_reverse(result *results, int len)
{
  for (int i = 0; i < len / 2; ++i) {
    result t = results[i];
    results[i] = results[len - i - 1];
    results[len - i - 1] = t;
  }
}

static int imax(int a, int b) { return a < b ? b : a; }
static int imin(int a, int b) { return a < b ? a : b; }

// Ensure idx is covered by [range_begin, range_end)
void
term_search_expand(int idx)
{
  int max_idx = term.cols * (term.sblines + term.rows);
  idx = imin(idx, max_idx - 1);
  idx = imax(idx, 0);

  // [range_1_begin, range_2_end) is the search region that covers [idx - look_around, idx + look_around)
  int look_around = term.cols * term.rows;    // chosen arbitrarily
  int pad = term.results.xquery_length * 2;   // the doubling is for UCSWIDE
  int range_1_begin = imax(idx - look_around - pad, 0);
  int range_2_end = imin(idx + look_around + pad, max_idx);

  // Previous range is empty, expand to [idx - look_around, idx + look_around).
  if (term.results.range_begin == term.results.range_end) {
    assert(term.results.length == 0);
    do_search(range_1_begin, range_2_end);
    term.results.range_begin = imax(idx - look_around, 0);
    term.results.range_end = imin(idx + look_around, max_idx);
  }
  // Expand range_begin, and append the results to term.results.results.
  // (Actually the results should be prepended instead of appended, we'll fix that later.)
  else if (idx < term.results.range_begin) {
    int previous_len = term.results.length;
    do_search(range_1_begin, term.results.range_begin);

    // The results from the expanding of range_begin were misplaced, fix it!
    int appended_len = term.results.length - previous_len;
    if (appended_len > 0 && previous_len > 0) {
      // <Previous_results> <Appended_results>
      results_reverse(term.results.results, previous_len);
      // <stluser_suoiverP> <Appended_results>
      results_reverse(term.results.results + previous_len, appended_len);
      // <stluser_suoiverP> <stluser_dedneppA>
      results_reverse(term.results.results, previous_len + appended_len);
      // <Appended_results> <Previous_results>
    }

    term.results.range_begin = imax(idx - look_around, 0);
  }
  // Expand range_end, and append the results to term.results.results.
  else if (idx >= term.results.range_end) {
    do_search(term.results.range_end, range_2_end);
    term.results.range_end = imin(idx + look_around, max_idx);
  }

  if (term.results.length > 0) {
    // Invariant: [range_begin, range_end) contains all results.
    result first = term.results.results[0];
    result last = term.results.results[term.results.length - 1];
    term.results.range_begin = imin(term.results.range_begin, first.idx);
    term.results.range_end = imax(term.results.range_end, last.idx + last.len);

    // Mark the current result (first result) if we can.
    if (term.results.current.len == 0 && term.results.range_begin == 0) {
      term.results.current = first;
    }
  }

  // Invariant: the results should be sorted and non-overlapping.
  for (int i = 1; i < term.results.length; ++i) {
    result prev = term.results.results[i - 1];
    assert(prev.idx + prev.len <= term.results.results[i].idx);
    (void)prev;
  }
  // Invariant: idx is covered by [range_begin, range_end).
  assert(term.results.range_begin <= idx && idx < term.results.range_end);
}

static result
results_find_ge(int idx)
{
  int b = 0;
  int e = term.results.length;
  while (b < e) {
    int m = (b + e) / 2;
    if (term.results.results[m].idx < idx) {
      b = m + 1;
    } else {
      e = m;
    }
  }

  if (b < term.results.length) {
    return term.results.results[b];
  } else {
    return (result) {0, 0};
  }
}

static result
results_find_le(int idx)
{
  int b = 0;
  int e = term.results.length;
  while (b < e) {
    int m = (b + e) / 2;
    if (term.results.results[m].idx > idx) {
      e = m;
    } else {
      b = m + 1;
    }
  }

  if (e > 0) {
    return term.results.results[e - 1];
  } else {
    return (result) {0, 0};
  }
}

#ifdef debug_search
static __inline__ uint64_t rdtsc(void)
{
  uint32_t hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}
#endif

result
term_search_next(void)
{
#ifdef debug_search
  uint64_t ts0 = rdtsc();
#endif

  result current = term.results.current;
  int max_idx = term.cols * (term.sblines + term.rows);

  // Search the region after current result.
  // If the current result was not marked, then idx == 0,
  // which means the upcoming search will return the first result in scrollback + screen.
  int idx = current.idx + current.len;
  int cycle_count = 0;
  while (true) {
    // Expand range_end to cover idx.
    term_search_expand(idx);
    // Check if the next result is covered.
    result found = results_find_ge(idx);
    if (found.len) {
#ifdef debug_search
      printf("term_search_next: cost: %lu\n", rdtsc() - ts0);
#endif
      return found;
    }

    // Not covered, advance idx to uncovered region.
    idx = term.results.range_end;

    if (idx >= max_idx) {
      // End of screen reached.
      if (current.len == 0) {
        // We have searched [0, max_idx), and no results were found.
        break;
      } else {
        // BUG! Crossing the boundary twice.
        // If the current result is marked, we should have found a result.
        assert(cycle_count == 0);
        if (cycle_count > 0) {
          break;
        }
      }
      cycle_count++;

      // Search from the beginning.
      idx = 0;
      if (term.results.range_begin != 0) {
        // Clear results before the next expansion to avoid full search.
        term_clear_results();
        // term.results.current should be preserved.
        term.results.current = current;
      }
    }
  }

  return (result) {0, 0};
}

result
term_search_prev(void)
{
  result current = term.results.current;
  int max_idx = term.cols * (term.sblines + term.rows);
  assert(max_idx > 0);

  // Search the region before current result.
  int idx = current.idx - 1;
  if (idx < 0) {
    idx = max_idx - 1;
  }
  int cycle_count = 0;
  while (true) {
    // Expand range_end to cover idx.
    term_search_expand(idx);
    // Check if the previous result is covered.
    result found = results_find_le(idx);
    if (found.len) {
      return found;
    }

    // Not covered, fall back idx to uncovered region.
    idx = term.results.range_begin - 1;

    if (idx < 0) {
      // Beginning of scrollback or screen reached.
      if (current.len == 0) {
        // We have searched [0, max_idx), and no results were found.
        break;
      } else {
        // BUG! Crossing the boundary twice.
        // If the current result is marked, we should have found a result.
        assert(cycle_count == 0);
        if (cycle_count > 0) {
          break;
        }
      }
      cycle_count++;

      // Search from the end.
      idx = max_idx - 1;
      if (term.results.range_end != max_idx) {
        // Clear results before the next expansion to avoid full search.
        term_clear_results();
        // term.results.current should be preserved.
        term.results.current = current;
      }
    }
  }

  return (result) {0, 0};
}

void
term_schedule_search_update(void)
{
  term.results.update_type = FULL_UPDATE;
}

void
term_clear_results(void)
{
  term.results.results = renewn(term.results.results, 16);
  term.results.current = (result) {0, 0};
  term.results.range_begin = term.results.range_end = 0;
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
  term.marg_left = 0;
  term.marg_right = newcols - 1;

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
    term.tabs[i] = term.newtab && (i % 8 == 0);

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
  if (to_alt == term.on_alt_screen)
    return;

  term.on_alt_screen = to_alt;

  termlines *oldlines = term.lines;
  term.lines = term.other_lines;
  term.other_lines = oldlines;

  /* swap image list */
  imglist * first = term.imgs.first;
  imglist * last = term.imgs.last;
  long long int offset = term.virtuallines;
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
    if (x == term.marg_right + 1)
      line->lattr &= ~LATTR_WRAPPED2;
    clear_cc(line, x - 1);
    clear_cc(line, x);
    line->chars[x - 1].chr = ' ';
    line->chars[x] = line->chars[x - 1];
  }
}


#ifdef use_display_scrolling

/*
   Scroll the actual display (window contents and its cache).
   NOTE: This was an incomplete and initially unsuccessful attempt 
   to implement smooth scrolling (DECSCLM, DECSET 4).
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
  bool down = scrolllines < 0;
  int lines = min(abs(scrolllines), term.rows);

  if (!win_do_scroll(topscroll, botscroll, lines))
    return;

  // update display cache
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

  if (term.lrmargmode && (term.marg_left || term.marg_right != term.cols - 1)) {
    scroll_rect(topline, botline, lines);
    return;
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

    // Move graphics if within the scroll region
    for (imglist * cur = term.imgs.first; cur; cur = cur->next) {
      if (cur->top - term.virtuallines >= topline) {
        cur->top += lines;
      }
    }
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

void
clear_wrapcontd(termline * line, int y)
{
  if (y < term.rows - 1 && (line->lattr & LATTR_WRAPPED)) {
    line = term.lines[y + 1];
    line->lattr &= ~(LATTR_WRAPCONTD | LATTR_AUTOSEL);
  }
}

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

  if (cfg.erase_to_scrollback && erasing_lines_from_top && 
      !(term.lrmargmode && (term.marg_left || term.marg_right != term.cols - 1))
     )
  {
   /* If it's a whole number of lines, starting at the top, and
    * we're fully erasing them, erase by scrolling and keep the
    * lines in the scrollback. This behaviour is not compatible with xterm. */
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
        clear_wrapcontd(line, start.y);
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
  wchar * efn;  // image filename
  void * buf;  // cached image
  int buflen;  // cached image
  struct {
    uint tags: 11;
    xchar ch: 21;
  } __attribute__((packed));
};

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
  wchar * efn;   // image filename
  void * buf;    // cached image
  int buflen;    // cached image
  echar chs[10]; // code points
  char * name;   // short name in emoji-sequences.txt, emoji-zwj-sequences.txt
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

void
clear_emoji_data()
{
  for (uint i = 0; i < lengthof(emoji_bases); i++) {
    if (emoji_bases[i].efn) {
      free(emoji_bases[i].efn);
      emoji_bases[i].efn = 0;
    }
    if (emoji_bases[i].buf) {
      free(emoji_bases[i].buf);
      emoji_bases[i].buf = 0;
      emoji_bases[i].buflen = 0;
    }
  }
  for (uint i = 0; i < lengthof(emoji_seqs); i++) {
    if (emoji_seqs[i].efn) {
      free(emoji_seqs[i].efn);
      emoji_seqs[i].efn = 0;
    }
    if (emoji_seqs[i].buf) {
      free(emoji_seqs[i].buf);
      emoji_seqs[i].buf = 0;
      emoji_seqs[i].buflen = 0;
    }
  }
}

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
    efnpoi = (wchar * *)&emoji_seqs[e.idx].efn;
  }
  else {
    efnpoi = (wchar * *)&emoji_bases[e.idx].efn;
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
  bool zwj = true; // include 200D in filename
  bool sel = true; // include FE0F in filename
  switch (style) {
#ifdef unicode_cldr
    when EMOJIS_CLDR:
      pre = "/usr/share/unicode/cldr/emoji/emoji_";
      sep = "_";
      zwj = true;
      sel = false;
#endif
    when EMOJIS_NOTO:
      pre = "noto/emoji_u";
      sep = "_";
      zwj = true;
      sel = false;
    when EMOJIS_JOYPIXELS:
      pre = "joypixels/";
      sep = "-";
      zwj = false;
      sel = false;
    when EMOJIS_ZOOM:
      pre = "zoom/";
      sep = "-";
      zwj = false;
      sel = false;
    when EMOJIS_ONE:
      pre = "emojione/";
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
    when EMOJIS_OPENMOJI:
      pre = "openmoji/";
      fmt = "%04X";
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

    * efnpoi = wcsdup(W(""));  // indicate "checked but not found"
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
  wchar * efn;
  void * * bufpoi;
  int * buflen;
  if (e.seq) {
    efn = emoji_seqs[e.idx].efn;
    bufpoi = &emoji_seqs[e.idx].buf;
    buflen = &emoji_seqs[e.idx].buflen;
  }
  else {
    efn = emoji_bases[e.idx].efn;
    bufpoi = &emoji_bases[e.idx].buf;
    buflen = &emoji_bases[e.idx].buflen;
  }
#ifdef debug_emojis
  printf("emoji_show @%d:%d..%d it %d seq %d idx %d <%ls>\n", y, x, elen, !!(eattr.attr & ATTR_ITALIC), e.seq, e.idx, efn);
#endif

  // Emoji overhang
  if (elen == 1 && (eattr.attr & TATTR_OVERHANG))
    elen = 2;
  //printf("emoj @%d:%d len %d\n", y, x, elen);
  if (efn && *efn)
    win_emoji_show(x, y, efn, bufpoi, buflen, elen, lattr, eattr.attr & ATTR_ITALIC);
}

#define dont_debug_win_text_invocation

#ifdef debug_win_text_invocation

void
_win_text(int line, int tx, int ty, wchar *text, int len, cattr attr, cattr *textattr, ushort lattr, bool has_rtl, bool clearpad, uchar phase)
{
  if (*text != ' ') {
    printf("[%d] %d:%d(len %d) attr %08llX", line, ty, tx, len, attr.attr);
    for (int i = 0; i < len && i < 8; i++)
      printf(" %04X", text[i]);
    printf("\n");
  }
  win_text(tx, ty, text, len, attr, textattr, lattr, has_rtl, clearpad, phase);
}

#define win_text(tx, ty, text, len, attr, textattr, lattr, has_rtl, clearpad, phase) _win_text(__LINE__, tx, ty, text, len, attr, textattr, lattr, has_rtl, clearpad, phase)

#endif

#define dont_debug_line
#define dont_debug_dirty 1

#ifdef debug_line
void trace_line(char * tag, termchar * chars)
{
  if (chars[0].chr > 0x80)
    printf("[%s] %04X %04X %04X %04X %04X %04X\n", tag, chars[0].chr, chars[1].chr, chars[2].chr, chars[3].chr, chars[4].chr, chars[5].chr);
}
#else
#define trace_line(tag, chars)	
#endif

#define UNLINED (UNDER_MASK | ATTR_STRIKEOUT | ATTR_OVERL | ATTR_OVERSTRIKE)
#define UNBLINK (FONTFAM_MASK | GRAPH_MASK | UNLINED | TATTR_EMOJI)

// Attributes to be ignored when checking whether to apply overhang:
// we cannot support overhang over double-width space (TATTR_WIDE),
// that would produce artefacts in the scrollback;
// need to ignore:
//	FONTFAM_MASK implementing option FontChoice
//	TATTR_NARROW which may have been set before this check
//	ATTR_BOLD | ATTR_DIM | TATTR_CLEAR which are used for clear space
#define IGNOVRHANG (FONTFAM_MASK | TATTR_NARROW | ATTR_BOLD | ATTR_DIM | TATTR_CLEAR)
#define at_cursor_pos(i, j)	((i == term.curs.y) && !((term.curs.x - j) >> 1))

#define IGNWIDTH TATTR_EXPAND | TATTR_NARROW | TATTR_SINGLE | TATTR_CLEAR
#define IGNEMOJATTR (TATTR_WIDE | ATTR_FGMASK | TATTR_COMBINING | IGNWIDTH)

void
term_paint(void)
{
  //if (kb_trace) printf("[%ld] term_paint\n", mtime());

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
    termline *line = fetch_line(scrpos.y);
    // Prevent nested emoji sequence matching from matching partial subseqs
    int emoji_col = 0;  // column from which to match for emoji sequences

    //trace_line("loop0", line->chars);

#if false
   /*
    * This was an attempt to support emojis within right-to-left text 
      introduced in 3.0.1 and removed for 3.5.1 because it spoiled 
      emoji selection highlighting (#1116), caused some flickering, 
      and did not even support right-to-left properly anyway.
      Bidi handling could be tweaked so that this approach would be applied 
      only in bidi lines, not fixing the other issues however.
      Previous comment:
    * Pre-loop: identify emojis and emoji sequences.
      This is a hacky approach to handling emoji sequences; problems:
      - temporary attributes are written back into original char matrix 
        rather than newchars, yielding:
      - if the emoji sequences gets broken later by partial overwriting,
        rendering will become inconsistent
      - rendering of emojis within bidi lines is only partially correct
      - selection highlighting does not work
      - rendering is applied repeatedly, resulting in some flickering
      Moving emoji sequences handling into the first loop below, on the 
      other hand, would break it even more in bidi lines.
    */
    for (int j = 0; j < term.cols; j++) {
      termchar *d = line->chars + j;
      cattr tattr = d->attr;

      // handle reverse attributes
      //...

      if (j < term.cols - 1 && d[1].chr == UCSWIDE)
        tattr.attr |= TATTR_WIDE;

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
            if ((d[i].attr.attr & ~IGNEMOJATTR) != (d->attr.attr & ~IGNEMOJATTR)
               || d[i].attr.truebg != d->attr.truebg
               )
              equalattrs = false;
          }
#ifdef debug_emojis
          printf("matched len %d seq %d idx %d ok %d atr %d\n", e.len, e.seq, e.idx, ok, equalattrs);
#endif

          // modify character data to trigger later emoji display
          if (ok && equalattrs) {
            // Emoji overhang
            if (e.len == 1 && j + 1 < term.cols
             // only if followed by space
             //&& iswspace(d[1].chr) && !d[1].cc_next
             //&& d[1].chr != 0x1680 && d[1].chr != 0x3000
             && d[1].chr == ' ' && !d[1].cc_next
             // not at cursor position? does not work for emojis
             //&& !at_cursor_pos(i, j)
             // and significant attributes are equal
             && !((d->attr.attr ^ d[1].attr.attr) & ~IGNOVRHANG)
             // do not overhang numbers, flag letters etc
             && d->chr >= 0x80 // exclude #️*️0️..9️
             //&& !(ch >= 0x1F1E6 || ch <= 0x1F1FF) // exclude 🇦..🇿
             && !(!e.seq && emoji_bases[e.idx].ch >= 0x1F1E6 && emoji_bases[e.idx].ch <= 0x1F1FF)
               )
            {
              d->attr.attr |= TATTR_OVERHANG;
              // also mark adjacent space to suppress its display
              d[1].attr.attr |= TATTR_OVERHANG;
            }

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
#ifdef handle_symbol_overhang_here
        else {  // not an emoji
        }
#endif
      }

      d->attr = tattr;
    }

    //trace_line("loopb", line->chars);
#endif

   /* Do Arabic shaping and bidi. */
    termchar *chars = term_bidi_line(line, i);
    int *backward = chars ? term.post_bidi_cache[i].backward : 0;
    int *forward = chars ? term.post_bidi_cache[i].forward : 0;
    chars = chars ?: line->chars;

    termline *displine = term.displines[i];
    termchar *dispchars = displine->chars;
    termchar newchars[term.cols];

    //trace_line("loop1", chars);

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

      if (cfg.printable_controls) {
        if (tchar >= 0x80 && tchar < 0xA0)
          tchar = 0x2592;  // ⌷⎕░▒▓
        else if (tchar < ' ' && cfg.printable_controls > 1)
          tchar = 0x2591;  // ⌷⎕░▒▓
        if (tchar >= 0x2580 && tchar <= 0x259F) {
          // Block Elements (U+2580-U+259F)
          // ▀▁▂▃▄▅▆▇█▉▊▋▌▍▎▏▐░▒▓▔▕▖▗▘▙▚▛▜▝▞▟
          tattr.attr |= ((cattrflags)(tchar & 0xF)) << ATTR_GRAPH_SHIFT;
          uchar gcode = 14 + ((tchar >> 4) & 1);
          // extend graph encoding with unused font numbers
          tattr.attr &= ~FONTFAM_MASK;
          tattr.attr |= (cattrflags)gcode << ATTR_FONTFAM_SHIFT;
        }
      }

      if (j < term.cols - 1 && d[1].chr == UCSWIDE)
        tattr.attr |= TATTR_WIDE;

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

     /* Colour indication for blinking ? */
      if ((tattr.attr & (ATTR_BLINK | ATTR_BLINK2))
       && term.enable_blink_colour && colours[BLINK_COLOUR_I] != (colour)-1
       //&& !(tattr.attr & TATTR_EMOJI)
         )
      {
        if (!(tattr.attr & TATTR_EMOJI)) {
          tattr.truefg = colours[BLINK_COLOUR_I];
          tattr.attr = (tattr.attr & ~ATTR_FGMASK) | (TRUE_COLOUR << ATTR_FGSHIFT);
        }
      }
     /* Real blinking ? */
      else if (term.blink_is_real) {
        if (tattr.attr & ATTR_BLINK2) {
          if (term.has_focus && term.tblinker2) {
            tattr.attr |= ATTR_INVISIBLE;
            tattr.attr &= ~UNBLINK;
          }
        }
        // ATTR_BLINK2 should override ATTR_BLINK to avoid chaotic dual blink
        else if (tattr.attr & ATTR_BLINK) {
          if (term.has_focus && term.tblinker) {
            tattr.attr |= ATTR_INVISIBLE;
            tattr.attr &= ~UNBLINK;
          }
        }
      }

      if (j < term.cols - 1 && d[1].chr == UCSWIDE)
        tattr.attr |= TATTR_WIDE;

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
            if ((d[i].attr.attr & ~IGNEMOJATTR) != (d->attr.attr & ~IGNEMOJATTR)
               || d[i].attr.truebg != d->attr.truebg
               )
              equalattrs = false;
          }
#ifdef debug_emojis
          printf("matched len %d seq %d idx %d ok %d atr %d\n", e.len, e.seq, e.idx, ok, equalattrs);
#endif

          // modify character data to trigger later emoji display
          if (ok && equalattrs) {
            // Emoji overhang
            if (e.len == 1 && j + 1 < term.cols
             // only if followed by space
             //&& iswspace(d[1].chr) && !d[1].cc_next
             //&& d[1].chr != 0x1680 && d[1].chr != 0x3000
             && d[1].chr == ' ' && !d[1].cc_next
             // not at cursor position? does not work for emojis
             //&& !at_cursor_pos(i, j)
             // and significant attributes are equal
             && !((d->attr.attr ^ d[1].attr.attr) & ~IGNOVRHANG)
             // do not overhang numbers, flag letters etc
             && d->chr >= 0x80 // exclude #️*️0️..9️
             //&& !(ch >= 0x1F1E6 || ch <= 0x1F1FF) // exclude 🇦..🇿
             && !(!e.seq && emoji_bases[e.idx].ch >= 0x1F1E6 && emoji_bases[e.idx].ch <= 0x1F1FF)
               )
            {
              d->attr.attr |= TATTR_OVERHANG;
              // also mark adjacent space to suppress its display
              d[1].attr.attr |= TATTR_OVERHANG;
            }

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
#ifdef handle_symbol_overhang_here
        else {  // not an emoji
        }
#endif
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
      // Emoji overhang
      if (tattr.attr & TATTR_OVERHANG) {
        // don't tamper with width of overhanging characters
      }
      else if (tchar >= 0xE0B0 && tchar < 0xE0C0) {
        // special handling for geometric "Powerline" symbols
        tattr.attr |= TATTR_ZOOMFULL;
        if (cs_ambig_wide) {
          tattr.attr |= TATTR_EXPAND;
        }
      }
#ifdef ignore_private_use_for_auto_narrowing
#warning auto-narrowing exemption for private use now handled by Symbol overhang below
      else if ((tchar >= 0xE000 && tchar < 0xF900)
            || (tchar >= 0xDB80 && tchar < 0xDC00)
              )
      {
        // don't tamper with width of Private Use characters
      }
#endif
      else if (tchar != dispchars[j].chr ||
          tattr.attr != (dispchars[j].attr.attr & ~(TATTR_NARROW | DATTR_MASK))
              )
      {
        xchar xch = tchar;
        if ((xch & 0xFC00) == 0xD800 && d->cc_next) {
          termchar * cc = d + d->cc_next;
          if ((cc->chr & 0xFC00) == 0xDC00) {
            xch = ((xchar) (xch - 0xD7C0) << 10) | (cc->chr & 0x03FF);
          }
        }

        if ((tattr.attr & TATTR_WIDE) == 0
            && cfg.char_narrowing < 100
            && win_char_width(xch, tattr.attr) == 2
            // && !(line->lattr & LATTR_MODE) ? "do not tamper with graphics"
            // && is_ambigwide(tchar) ? but then they will be clipped...
           )
        {
          //printf("[%d:%d] narrow? %04X..%04X\n", i, j, tchar, chars[j + 1].chr);
          if (
              // do not narrow various symbol ranges;
              // this is a bit redundant with Symbol overhang below
                 (xch >= 0x2190 && xch <= 0x25FF)
              || (xch >= 0x27C0 && xch <= 0x2BFF)
             )
          {
            //tattr.attr |= TATTR_NARROW1; // ?
          }
          else {
#ifdef failed_attempt_to_tame_narrowing
#warning this is now handled for specific character ranges by Symbol overhang below
            if (j + 1 < term.cols && chars[j + 1].chr != ' ')
#endif
            tattr.attr |= TATTR_NARROW;
            //if (ch != 0x25CC)
            //printf("char %lc U+%04X narrow %d ambig %d\n", xch, xch, !!(tattr.attr & TATTR_NARROW), is_ambigwide(xch));
          }
        }
        else if ((tattr.attr & TATTR_WIDE)
                 // guard character expanding properly to avoid 
                 // false hits as reported for CJK in #570,
                 // considering that Windows may report width 1 
                 // for double-width characters 
                 // (if double-width by font substitution)
                 && cs_ambig_wide
                 // the following restriction would be good for
                 // MS PGothic (but bad non-CJK range anyway)
                 // but bad for
                 // MS Mincho: wide Greek/Cyrillic but narrow æ, œ, ...
                 // SimSun, NSimSun, Yu Gothic
                 //&& !font_ambig_wide
                 && win_char_width(xch, tattr.attr) == 1
                 // and reassure to apply this only to ambiguous width chars
                 && is_ambigwide(tchar) // is_ambig(tchar) && !is_wide(tchar)
                 // do not widen Geometric Shapes
                 // (Geometric Shapes Extended are not ambiguous)
                 && !(0x25A0 <= tchar && tchar <= 0x25FF)
                )
        {
          tattr.attr |= TATTR_EXPAND;
        }
      }
      else if (dispchars[j].attr.attr & TATTR_NARROW) {
        tattr.attr |= TATTR_NARROW;
      }

     /* Symbol overhang */
      /* Note: to consider Visible space indication for overhang
         (not to be applied then), there are two options:
         - move this into the second loop below (first attempt failed)
         - cancel overhang when adjusting next position with indication
      */
      //if (tchar >= 0x0900) printf("@%d %08llX %08llX\n", j, tattr.attr, chars[j + 1].attr.attr);
      if (tchar >= 0x0900 && j + 1 < term.cols
       // only if followed by space
       //&& iswspace(chars[j + 1].chr) && !chars[j + 1].cc_next
       //&& chars[j + 1].chr != 0x1680
       && chars[j + 1].chr == ' ' && !chars[j + 1].cc_next
       // not at cursor position? does not seem proper
       //&& !at_cursor_pos(i, j)
       // and significant attributes are equal
       && !((tattr.attr ^ chars[j + 1].attr.attr) & ~IGNOVRHANG)
         )
      {
        //printf("symb @%d:%d overhang %d attr %08llX attr1 %08llX\n", i, j, !!(chars[j].attr.attr & TATTR_OVERHANG), chars[j].attr.attr, chars[j + 1].attr.attr);
        if (
             (tchar >= 0x20A0 && tchar < 0x2400)  // Symbols
          || (tchar >= 0x2460 && tchar < 0x2500)  // Symbols
          || (tchar >= 0x25A0 && tchar < 0x2800)  // Symbols
          || (tchar >= 0x2900 && tchar < 0x2C00)  // Symbols
          || (tchar >= 0xE000 && tchar < 0xF900)  // Private Use
          || (tchar >= 0xDB80 && tchar <= 0xDBFF)  // Private Use
          || (tchar >= 0xDB3C && tchar <= 0xD83E)  // Symbols
          || indicwide(tchar) || extrawide(tchar)
           )
        {
          tattr.attr |= TATTR_OVERHANG;
          // clear narrowing; this is rather done in the second loop below;
          // as we still need this information in case the overhang flag 
          // is reset when handling the subsequent position
          //tattr.attr &= ~TATTR_NARROW;
        }
      }

#define dont_debug_width_scaling
#ifdef debug_width_scaling
      if (tattr.attr & (TATTR_EXPAND | TATTR_NARROW | TATTR_WIDE))
        printf("%04X w %d enw %02X\n", tchar, win_char_width(tchar, tattr.attr), (uint)(((tattr.attr & (TATTR_EXPAND | TATTR_NARROW | TATTR_WIDE)) >> 24)));
#endif

     /* Visible space indication */
      if (tchar == ' ') {
        int disp = 0;
        if (tattr.attr & TATTR_CLEAR) {
          // TAB indication
          if (cfg.disp_tab) {
            static wchar tab[] = W("▹►"); // ▹▹► ▻▻► ▹▹▶ ▷▷▶ ››» ▸▸▶ ▹▹▷ ▹▹▸
            if (tattr.attr & ATTR_BOLD) {
              tchar = tab[1];
              disp = cfg.disp_tab;
            }
            else if (tattr.attr & ATTR_DIM) {
              tchar = tab[0];
              disp = cfg.disp_tab;
            }
          }
          tattr.attr &= ~(ATTR_BOLD | ATTR_DIM);

          if (!(cfg.disp_clear & 8))
            tattr.attr &= ~TATTR_CLEAR;
          if (!disp)
            disp = cfg.disp_clear & ~8;
        }
        else
          disp = cfg.disp_space;

        if (disp) {
          if (tchar == ' ')
            tchar = 0xB7; // ·0x00B7 ⋯0x22EF
          if (disp & 1)
            tattr.attr |= ATTR_BOLD;
          if (disp & 2)
            tattr.attr |= ATTR_DIM;
          if ((disp & 4) && cfg.underl_colour != (colour)-1) {
            tattr.truefg = cfg.underl_colour;
            tattr.attr |= TRUE_COLOUR << ATTR_FGSHIFT;
          }

          // Symbol overhang: cancel overhang of previous character
          // (reason to keep TATTR_NARROW in this loop)
          if (j)
            newchars[j - 1].attr.attr &= ~TATTR_OVERHANG;
        }
      }

     /* FULL-TERMCHAR */
      newchars[j].attr = tattr;
      newchars[j].chr = tchar;
     /* Combining characters are still read from chars */
      newchars[j].cc_next = 0;
    }  // end first loop

    if (i == curs_y) {
     /* Determine the column the cursor is on, taking bidi into account and
      * moving it one column to the left when it's on the right half of a
      * wide character.
      */
      int curs_x = term.curs.x;
      if (forward)
        curs_x = forward[curs_x];
#ifdef support_triple_width
      while (curs_x > 0 && chars[curs_x].chr == UCSWIDE)
        curs_x--;
#else
      if (curs_x > 0 && chars[curs_x].chr == UCSWIDE)
        curs_x--;
#endif

     /* Determine cursor cell attributes. */
      newchars[curs_x].attr.attr |=
        (!term.has_focus ? TATTR_PASCURS :
         term.cblinker || !term_cursor_blinks() ? TATTR_ACTCURS : 0) |
        (term.curs.wrapnext ? TATTR_RIGHTCURS : 0);

      if (term.cursor_invalid)
#ifdef debug_dirty
        printf("INVALID c %d\n", curs_x),
#endif
        dispchars[curs_x].attr.attr |= ATTR_INVALID;

      // try to fix #612 "cursor isn’t hidden right away"
      if (newchars[curs_x].attr.attr != dispchars[curs_x].attr.attr)
#ifdef debug_dirty
        printf("INVALID c %d\n", curs_x),
#endif
        dispchars[curs_x].attr.attr |= ATTR_INVALID;

     /* Progress indication */
      if (term.detect_progress) {
        int j = term.cols;
        while (--j > 0) {
          if (chars[j].chr == '%'
#ifdef detect_percentage_only_at_line_end
              ||
              // check empty space indication in chars;
              // note: TATTR_CLEAR is already cleared in newchars
              (!(chars[j].attr.attr & TATTR_CLEAR)
               // accept anything after %
               && 0
               // accept termination chars after percentage indication
               && !wcschr(W(")]."), chars[j].chr)
              )
#endif
             )
            break;
        }
        int p = 0;
        if (chars[j].chr == '%' && (displine->lattr & LATTR_PROGRESS)) {
          int f = 1;
          while (--j >= 0) {
            if (chars[j].chr >= '0' && chars[j].chr <= '9') {
              p += f * (chars[j].chr - '0');
              f *= 10;
            }
            else if (chars[j].chr == '.' || chars[j].chr == ',') {
              p = 0;
              f = 1;
            }
            else {
              j++;
              break;
            }
          }
        }
        if (p <= 100) {
          taskbar_progress(- term.detect_progress);
          taskbar_progress(p);
        }
      }
    }

    //trace_line("loopn", newchars);
    //trace_line("loopd", dispchars);

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
        if (firstitalicstart < 0 && newchars[j].attr.attr & (ATTR_ITALIC | TATTR_OVERHANG))
          firstitalicstart = j;
      }

     /* Symbol overhang: clear narrowing */
      if (newchars[j].attr.attr & TATTR_OVERHANG)
        newchars[j].attr.attr &= ~TATTR_NARROW;

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
#ifdef debug_dirty
        printf("INVALID %d..%d\n", start, j - 1);
#endif
        for (int k = start; k < j; k++)
          dispchars[k].attr.attr |= ATTR_INVALID;
        dirtyrect = true;
        prevdirtyitalic = false;
      }
      if (dirtyrect && dispchars[j].attr.attr & (ATTR_ITALIC | TATTR_OVERHANG))
        prevdirtyitalic = true;
      else if (dispchars[j].attr.attr & DATTR_STARTRUN)
        prevdirtyitalic = false;
      if (j == 0)
        firstdirtyitalic = prevdirtyitalic;

      if (dirtyrect)
#ifdef debug_dirty
        printf("INVALID %d\n", j),
#endif
        dispchars[j].attr.attr |= ATTR_INVALID;
    }
    if (prevdirtyitalic) {
      // clear overhang into right padding border
      win_text(term.cols, i, W(" "), 1, CATTR_DEFAULT, (cattr*)&CATTR_DEFAULT, line->lattr, false, true, 0);
    }
    if (firstdirtyitalic) {
      // clear overhang into left padding border
      win_text(-1, i, W(" "), 1, CATTR_DEFAULT, (cattr*)&CATTR_DEFAULT, line->lattr, false, true, 0);
    }

#define dont_debug_bidi_paragraphs
#ifdef debug_bidi_paragraphs
    static cattr CATTR_WRAPPED = {.truebg = RGB(255, 0, 0),
                             .attr = ATTR_DEFFG | TRUE_COLOUR << ATTR_BGSHIFT,
                             .truefg = 0, .ulcolr = (colour)-1, .link = -1};
    static cattr CATTR_CONTD = {.truebg = RGB(0, 0, 255),
                             .attr = ATTR_DEFFG | TRUE_COLOUR << ATTR_BGSHIFT,
                             .truefg = 0, .ulcolr = (colour)-1, .link = -1};
    static cattr CATTR_CONTWRAPD = {.truebg = RGB(255, 0, 255),
                             .attr = ATTR_DEFFG | TRUE_COLOUR << ATTR_BGSHIFT,
                             .truefg = 0, .ulcolr = (colour)-1, .link = -1};
    wchar diag = ((line->lattr & 0x0F00) >> 8) + '0';
    if (diag > '9')
      diag += 'A' - '0' - 10;
    if (line->lattr & (LATTR_WRAPPED | LATTR_WRAPCONTD)) {
      if ((line->lattr & (LATTR_WRAPPED | LATTR_WRAPCONTD)) == (LATTR_WRAPPED | LATTR_WRAPCONTD))
        win_text(-1, i, &diag, 1, CATTR_CONTWRAPD, (cattr*)&CATTR_CONTWRAPD, line->lattr, false, true, 0);
      else if (line->lattr & LATTR_WRAPPED)
        win_text(-1, i, &diag, 1, CATTR_WRAPPED, (cattr*)&CATTR_WRAPPED, line->lattr, false, true, 0);
      else
        win_text(-1, i, &diag, 1, CATTR_CONTD, (cattr*)&CATTR_CONTD, line->lattr, false, true, 0);
    }
    else if (displine->lattr & (LATTR_WRAPPED | LATTR_WRAPCONTD))
      win_text(-1, i, W(" "), 1, CATTR_DEFAULT, (cattr*)&CATTR_DEFAULT, line->lattr, false, true, 0);
#endif

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
#if defined(debug_dirty) && debug_dirty > 1
    printf("dirty ini %d:* lin %d run %d\n", i, dirty_line, dirty_run);
#endif
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
        win_text(ovl_x, ovl_y, ovl_text, ovl_len, ovl_attr, ovl_textattr, ovl_lattr, ovl_has_rtl, false, 2);
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
        eattr.attr &= ~(TATTR_WIDE | TATTR_COMBINING);
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

            // Emoji overhang
            if (elen == 1 && attr.attr & TATTR_OVERHANG)
              elen = 2;
            // fill emoji background
            win_text(x, y, esp, elen, eattr, textattr, lattr, has_rtl, false, 1);
            flush_text();
          }
#if defined(debug_emojis) && debug_emojis > 3
          // add background to some emojis
          eattr.attr &= ~(ATTR_BGMASK | ATTR_FGMASK);
          eattr.attr |= 6 << ATTR_BGSHIFT | 4;
          esp[0] = '0' + elen;
          win_text(x, y, esp, elen, eattr, textattr, lattr, has_rtl, false, 2);
#endif
          if (cfg.emoji_placement == EMPL_FULL && !overlaying)
            do_overlay = true;  // display in overlaying loop
          else {
            //struct emoji e = (struct emoji) eattr.truefg;
            struct emoji * ee = (void *)&eattr.truefg;
            emoji_show(x, y, *ee, elen, eattr, lattr);
          }
        }
#if defined(debug_emojis) && debug_emojis > 3
        else { // mark some emojis
          eattr.attr &= ~(ATTR_BGMASK | ATTR_FGMASK);
          eattr.attr |= 4 << ATTR_BGSHIFT | 6;
          esp[0] = '0';
          win_text(x, y, esp, 1, eattr, textattr, lattr, has_rtl, false, 2);
        }
#endif
      }
      else if ((attr.attr & TATTR_OVERHANG) && *text == ' ' //iswspace(*text)
             // skip the skipping if overhanging char was meanwhile changed
             && start && (newchars[start - 1].attr.attr & TATTR_OVERHANG)
              )
      {
        // Emoji overhang
        // do not output adjacent space after overhanging emoji;
        return;
      }
      else if (overlaying) {
        return;
      }
      else if (attr.attr & (ATTR_ITALIC | TATTR_COMBDOUBL | TATTR_OVERHANG)) {
        win_text(x, y, text, len, attr, textattr, lattr, has_rtl, false, 1);
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
        win_text(x, y, text, len, attr, textattr, lattr, has_rtl, false, 0);
        flush_text();
      }
    }

    //trace_line("loop3", newchars);

   /*
    * Third loop, for actual drawing.
    */
    for (int j = 0; j < term.cols; j++) {
      termchar *d = chars + j;
      cattr tattr = newchars[j].attr;
      wchar tchar = newchars[j].chr;
#ifdef support_triple_width
      if (tchar == UCSWIDE) {
        continue;
      }
#endif

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

      if ((dispchars[j].attr.attr ^ tattr.attr) & TATTR_WIDE)
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

      if (tchar != SIXELCH && (tattr.attr & TATTR_NARROW))
        trace_run("narrow"), break_run = true;

      if (tattr.attr & TATTR_EMOJI)
        trace_run("emoji"), break_run = true;

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

     /*
      * Break some special ranges to avoid erratic narrow spacing.
      */
      if (j > 0 && (d[-1].chr | 1) == 0xFD3F)
        trace_run("spec"), break_run = true;

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

      if (break_run || cfg.bloom) {
        if ((dirty_run && textlen) || overlaying)
          out_text(start, i, text, textlen, attr, textattr, line->lattr, has_rtl);
        start = j;
        textlen = 0;
        has_rtl = false;
        attr = tattr;
        dirty_run = dirty_line;
#if defined(debug_dirty) && debug_dirty > 1
        printf("dirty brk %d:%d lin %d run %d\n", i, j, dirty_line, dirty_run);
#endif
      }

      bool do_copy =
        !termchars_equal_override(&dispchars[j], d, tchar, tattr);
      dirty_run |= do_copy;
#if defined(debug_dirty) && debug_dirty > 1
      printf("dirty cop %d:%d lin %d run %d\n", i, j, dirty_line, dirty_run);
#endif

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

     /* Append combining and overstrike characters, combine surrogates */
      if (d->cc_next) {
        termchar *dd = d;
        while (dd->cc_next && textlen < maxtextlen) {
#ifdef debug_surrogates
          wchar prev = dd->chr;
#endif
          dd += dd->cc_next;
          wchar tchar = dd->chr;

          // mark combining unless pseudo-combining surrogates
          if (!is_low_surrogate(tchar)) {
            if (tattr.attr & TATTR_EMOJI)
              break;
            attr.attr |= TATTR_COMBINING;
          }
          if (combiningdouble(tchar))
            attr.attr |= TATTR_COMBDOUBL;

          // copy attribute, handle blinking
          cattr tattr = dd->attr;
          if ((tattr.attr & (ATTR_BLINK | ATTR_BLINK2))
           && term.enable_blink_colour && colours[BLINK_COLOUR_I] != (colour)-1
           && !(tattr.attr & TATTR_EMOJI)
             )
          {  // colour indication for blinking
            tattr.truefg = colours[BLINK_COLOUR_I];
            tattr.attr = (tattr.attr & ~ATTR_FGMASK) | (TRUE_COLOUR << ATTR_FGSHIFT);
          }
          else if (term.blink_is_real) {
            if (tattr.attr & ATTR_BLINK2) {
              if (term.has_focus && term.tblinker2) {
                tattr.attr |= ATTR_INVISIBLE;
                tattr.attr &= ~UNBLINK;
              }
              dirty_run = true;  // attempt to optimise this failed
            }
            // ATTR_BLINK2 should override ATTR_BLINK to avoid chaotic dual blink
            else if (tattr.attr & ATTR_BLINK) {
              if (term.has_focus && term.tblinker) {
                tattr.attr |= ATTR_INVISIBLE;
                tattr.attr &= ~UNBLINK;
              }
              dirty_run = true;  // attempt to optimise this failed
            }
          }
          textattr[textlen] = tattr;

          if (cfg.emojis && tchar == 0xFE0E)
            ; // skip text style variation selector
          else if (tchar >= 0x2066 && tchar <= 0x2069)
            // hide bidi isolate mark glyphs (if handled zero-width)
            text[textlen++] = 0x200B;  // zero width space
          else
            text[textlen++] = tchar;
#ifdef debug_surrogates
          ucschar comb = 0xFFFFF;
          if ((prev & 0xFC00) == 0xD800 && (tchar & 0xFC00) == 0xDC00)
            comb = ((ucschar) (prev - 0xD7C0) << 10) | (tchar & 0x03FF);
          printf("comb (%04X) %04X %04X (%05X) %11llX\n", 
                 d->chr, prev, tchar, comb, attr.attr);
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
      if ((tattr.attr & TATTR_WIDE) && ++j < term.cols) {
        d++;
       /*
        * By construction above, the cursor should not
        * be on the right-hand half of this character.
        * Ever.
        */
        if (!termchars_equal(&dispchars[j], d)) {
          dirty_run = true;
#if defined(debug_dirty) && debug_dirty > 1
          printf("dirty neq %d:%d lin %d run %d\n", i, j, dirty_line, dirty_run);
#endif
        }
        copy_termchar(displine, j, d);
#ifdef support_triple_width
#warning do not handle triple-width here
        //while (dispchars[j].chr == UCSWIDE) {
        //  j++;
        //  start++;
        //}
#endif
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
      ushort lattr = line->lattr;
      release_line(line);
      if (lattr & LATTR_MARKED) {
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

  if (has_focus != term.focus_reported) {
    term.focus_reported = has_focus;

static bool sys_scroll_lock;
    if (has_focus) {
      sys_scroll_lock = get_scroll_lock();
      sync_scroll_lock(term.no_scroll || term.scroll_mode);
    }
    else
      sync_scroll_lock(sys_scroll_lock);

    if (term.report_focus && may_report)
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

