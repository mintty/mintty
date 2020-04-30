#include "term.h"
#include "charset.h"


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

static uint16_t case_folding_small[256] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
  22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
  42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
  62, 63, 64, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
  111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 91, 92, 93, 94, 95,
  96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
  113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
  129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
  145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
  161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176,
  177, 178, 179, 180, 956 /* 956 > 255 ??? */, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 224,
  225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
  241, 242, 243, 244, 245, 246, 215, 248, 249, 250, 251, 252, 253, 254, 223, 224,
  225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240,
  241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};
#endif

static uint
case_fold(uint ch)
{
#ifndef dynamic_casefolding
  if (ch < 256) {
    // this reduces search time by 40%
    return case_folding_small[ch];
  }
#endif

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

// return search results contained by [begin, end)
static void
do_search(int begin, int end) {
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
      result run = {
        .idx = cpos - anpos + 1,
        .len = anpos
      };
      assert(begin <= run.idx && (run.idx + run.len) < end);
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
