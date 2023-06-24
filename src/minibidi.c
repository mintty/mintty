#include "minibidi.h"
#include "term.h"  // UCSWIDE

/************************************************************************
 * $Id: minibidi.c 6910 2006-11-18 15:10:48Z simon $
 *
 * ------------
 * Description:
 * ------------
 * This is an implementation of Unicode's Bidirectional Algorithm
 * (known as UAX #9).
 *
 *   http://www.unicode.org/reports/tr9/
 *
 * Author: Ahmad Khalifa
 * (www.arabeyes.org - under MIT license)
 *
 * Modified: Thomas Wolff:
 * - extended bidi class detection to consider non-BMP characters
 * - generating mirroring data from Unicode
 * - fixed recognition of runs
 * - sanitized handling of directional markers
 * - support bidi mode model of «BiDi in Terminal Emulators» recommendation
     (https://terminal-wg.pages.freedesktop.org/bidi/):
 *   - return resolved paragraph level (or -1 if none)
 *   - switchable options for:
 *     - autodetection (rules P2, P3)
 *     - LTR/RTL fallback for autodetection
 *     - explicit RTL
 *     - mirroring of additional characters (Box drawing, quadrant blocks)
 * - fixed UBA shortcut which ignored AN
 * - implemented UBA rule N0 (bracket pair handling)
 * - fixed X9 to mask formatters with NSM rather than BN
 * - fixed W7 to fallback to sor
 * - fixed L1 to skip directional markers
 *
 ************************************************************************/

/*
 * TODO:
 * =====
 * - Explicit markers need to be handled (they are not 100% now),
     esp. isolate markers
 * - Ligatures (?)
 */


/* Shaping Helpers */
#define STYPE(xh) ((((xh) >= SHAPE_FIRST) && ((xh) <= SHAPE_LAST)) ? \
shapetypes[(xh)-SHAPE_FIRST].type : SU) /*)) */
#define SISOLATED(xh) \
  (0xFE00 + (shapetypes[(xh)-SHAPE_FIRST].form_b ?: -0xFE00))
#define SFINAL(xh) ((xh)+1)
#define SINITIAL(xh) ((xh)+2)
#define SMEDIAL(ch) ((ch)+3)

#define leastGreaterOdd(x) ( ((x)+1) | 1 )
#define leastGreaterEven(x) ( ((x)+2) &~ 1 )

/* Shaping Types */
enum {
  SL,   /* Left-Joining, doesn't exist in U+0600 - U+06FF */
  SR,   /* Right-Joining, i.e. has Isolated, Final */
  SD,   /* Dual-Joining, i.e. has Isolated, Final, Initial, Medial */
  SU,   /* Non-Joining */
  SC    /* Join-Causing, like U+0640 (TATWEEL) */
};

typedef struct {
  uchar type;
  uchar form_b;
} shape_node;

/* Kept near the actual table, for verification. */
enum { SHAPE_FIRST = 0x621, SHAPE_LAST = 0x64A };

static const shape_node shapetypes[] = {
 /* index, Typ, Iso, Ligature Index */
 /* 621 */ {SU, 0x80},
 /* 622 */ {SR, 0x81},
 /* 623 */ {SR, 0x83},
 /* 624 */ {SR, 0x85},
 /* 625 */ {SR, 0x87},
 /* 626 */ {SD, 0x89},
 /* 627 */ {SR, 0x8D},
 /* 628 */ {SD, 0x8F},
 /* 629 */ {SR, 0x93},
 /* 62A */ {SD, 0x95},
 /* 62B */ {SD, 0x99},
 /* 62C */ {SD, 0x9D},
 /* 62D */ {SD, 0xA1},
 /* 62E */ {SD, 0xA5},
 /* 62F */ {SR, 0xA9},
 /* 630 */ {SR, 0xAB},
 /* 631 */ {SR, 0xAD},
 /* 632 */ {SR, 0xAF},
 /* 633 */ {SD, 0xB1},
 /* 634 */ {SD, 0xB5},
 /* 635 */ {SD, 0xB9},
 /* 636 */ {SD, 0xBD},
 /* 637 */ {SD, 0xC1},
 /* 638 */ {SD, 0xC5},
 /* 639 */ {SD, 0xC9},
 /* 63A */ {SD, 0xCD},
 /* 63B */ {SU, 0x0},
 /* 63C */ {SU, 0x0},
 /* 63D */ {SU, 0x0},
 /* 63E */ {SU, 0x0},
 /* 63F */ {SU, 0x0},
 /* 640 */ {SC, 0x0},
 /* 641 */ {SD, 0xD1},
 /* 642 */ {SD, 0xD5},
 /* 643 */ {SD, 0xD9},
 /* 644 */ {SD, 0xDD},
 /* 645 */ {SD, 0xE1},
 /* 646 */ {SD, 0xE5},
 /* 647 */ {SD, 0xE9},
 /* 648 */ {SR, 0xED},
 /* 649 */ {SR, 0xEF},
 /* 64A */ {SD, 0xF1}
};


/*
 * Finds the index of a run with level tlevel or higher (!)
 */
static int
find_run(uchar * levels, int start, int count, int tlevel)
{
  for (int i = start; i < count; i++) {
    if (tlevel <= levels[i]) {
      return i;
    }
  }
  return count;
}

/*
 * Flips runs in text buffer, of tlevel and all higher levels
 *
 * Input:
 * from: text buffer, on which to apply flipping
 * levels: resolved levels buffer
 * tlevel: the level from which to flip runs
 * count: line size in bidi_char and levels
 */
static void
flip_runs(bidi_char * from, uchar * levels, int tlevel, int count)
{
  int i, j;

  j = i = 0;
  while (i < count && j < count) {
   /* find the start of the next run */
    i = j = find_run(levels, i, count, tlevel);
   /* find the end of the run */
    while (i < count && tlevel <= levels[i]) {
      i++;
    }
    for (int k = i - 1; k > j; k--, j++) {
      bidi_char temp = from[k];
      from[k] = from[j];
      from[j] = temp;
    }
  }
}

/*
 * Returns the bidi character type of ch.
 *
 * The data table in this function is constructed from the Unicode
 * Character Database by the script mkbidiclasses
 */
uchar
bidi_class(ucschar ch)
{
  static const struct {
    ucschar first, last;
    uchar type;
  } lookup[] = {
#include "bidiclasses.t"
  };

  int i, j, k;

  i = -1;
  j = lengthof(lookup);

  while (j - i > 1) {
    k = (i + j) / 2;
    if (ch < lookup[k].first)
      j = k;
    else if (ch > lookup[k].last)
      i = k;
    else
      return lookup[k].type;
  }

 /*
  * If we reach here, the character was not in any of the
  * intervals listed in the lookup table. This means we return
  * ON (`Other Neutrals'). This is the appropriate code for any
  * character genuinely not listed in the Unicode table, and
  * also the table above has deliberately left out any
  * characters _explicitly_ listed as ON (to save space!).
  */
  return ON;
}

/*
 * Function exported to front ends to allow them to identify
 * bidi-active characters (in case, for example, the platform's
 * text display function can't conveniently be prevented from doing
 * its own bidi and so special treatment is required for characters
 * that would cause the bidi algorithm to activate).
 * 
 * This function is passed a single Unicode code point, and returns
 * nonzero if the presence of this code point can possibly cause
 * the bidi algorithm to do any reordering. Thus, any string
 * composed entirely of characters for which is_rtl() returns zero
 * should be safe to pass to a bidi-active platform display
 * function without fear.
 * 
 * (is_rtl() must therefore also return true for any character
 * which would be affected by Arabic shaping, but this isn't
 * important because all such characters are right-to-left so it
 * would have flagged them anyway.)
 */
//#define is_rtl(c)	is_rtl_class(bidi_class(c))
bool
is_rtl_class(uchar bc)
{
 /*
  * After careful reading of the Unicode bidi algorithm (URL as
  * given at the top of this file) I believe that the only
  * character classes which can possibly cause trouble are R,
  * AL, RLE and RLO. I think that any string containing no
  * character in any of those classes will be displayed
  * uniformly left-to-right by the Unicode bidi algorithm.
  */
  const int mask = (1 << R) | (1 << AL) | (1 << RLE) | (1 << RLO)
                 | (1 << RLI) | (1 << FSI)
                 ;

  return mask & (1 << (bc));
}

bool
is_sep_class(uchar bc)
{
  const int mask = (1 << B) | (1 << S) | (1 << BN) | (1 << WS) | (1 << ON);

  return mask & (1 << (bc));
}

bool
is_punct_class(uchar bc)
{
  const int mask = (1 << BN) | (1 << CS) | (1 << EN) | (1 << ES) | (1 << ET);

  return mask & (1 << (bc));
}

static bool
is_NI(uchar bc)
{
  const int mask = (1 << B) | (1 <<  S) | (1 <<  WS) | (1 <<  ON) | (1 <<  FSI) | (1 <<  LRI) | (1 <<  RLI) | (1 <<  PDI);

  return mask & (1 << (bc));
}


/* The Main shaping function, and the only one to be used
 * by the outside world.
 *
 * line: buffer to apply shaping to. this must be passed by doBidi() first
 * to: output buffer for the shaped data
 * count: number of characters in line
 */
int
do_shape(bidi_char * line, bidi_char * to, int count)
{
  int i, tempShape, ligFlag;

  for (ligFlag = i = 0; i < count; i++) {
    to[i] = line[i];
    tempShape = STYPE(line[i].wc);
    switch (tempShape) {
      when SR:
        tempShape = (i + 1 < count ? STYPE(line[i + 1].wc) : SU);
        if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
          to[i].wc = SFINAL((SISOLATED(line[i].wc)));
        else
          to[i].wc = SISOLATED(line[i].wc);
      when SD: {
       /* Make Ligatures */
        tempShape = (i + 1 < count ? STYPE(line[i + 1].wc) : SU);
        if (line[i].wc == 0x644) {
          if (i > 0)
            switch (line[i - 1].wc) {
              when 0x622:
                ligFlag = 1;
                if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                  to[i].wc = 0xFEF6;
                else
                  to[i].wc = 0xFEF5;
              when 0x623:
                ligFlag = 1;
                if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                  to[i].wc = 0xFEF8;
                else
                  to[i].wc = 0xFEF7;
              when 0x625:
                ligFlag = 1;
                if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                  to[i].wc = 0xFEFA;
                else
                  to[i].wc = 0xFEF9;
              when 0x627:
                ligFlag = 1;
                if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                  to[i].wc = 0xFEFC;
                else
                  to[i].wc = 0xFEFB;
            }
          if (ligFlag) {
            to[i - 1].wc = 0x20;
            ligFlag = 0;
            break;
          }
        }

        if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC)) {
          tempShape = (i > 0 ? STYPE(line[i - 1].wc) : SU);
          if ((tempShape == SR) || (tempShape == SD) || (tempShape == SC))
            to[i].wc = SMEDIAL((SISOLATED(line[i].wc)));
          else
            to[i].wc = SFINAL((SISOLATED(line[i].wc)));
          break;
        }

        tempShape = (i > 0 ? STYPE(line[i - 1].wc) : SU);
        if ((tempShape == SR) || (tempShape == SD) || (tempShape == SC))
          to[i].wc = SINITIAL((SISOLATED(line[i].wc)));
        else
          to[i].wc = SISOLATED(line[i].wc);
      }
    }
  }
  return 1;
}

enum { BRACKx, BRACKo, BRACKc };

static ucschar
bracket(ucschar c)
{
  static const struct {
    wchar from, to;
    uchar bracket;
  } pairs[] = {
#include "brackets.t"
  };

  int i = -1;
  int j = lengthof(pairs);
  while (j - i > 1) {
    int k = (i + j) / 2;
    if (c == pairs[k].from) {
      if (pairs[k].bracket == BRACKo)
        return c;
      else
        return pairs[k].to;
    }
    if (c < pairs[k].from)
      j = k;
    else
      i = k;
  }

  return 0;
}

/*
  Determine canonical equivalent of a bracket character.
 */
static ucschar
canonical(ucschar c)
{
  static const struct {
    wchar bracket, canonical;
  } pairs[] = {
#include "canonical.t"
  };

  int i = -1;
  int j = lengthof(pairs);
  while (j - i > 1) {
    int k = (i + j) / 2;
    if (c == pairs[k].bracket)
      return pairs[k].canonical;
    if (c < pairs[k].bracket)
      j = k;
    else
      i = k;
  }

  return c;
}

static ucschar
mirror(ucschar c, bool box_mirror)
{
  static const struct { wchar from, to; } pairs[] = {
#include "mirroring.t"
  };

  int i = -1;
  int j = lengthof(pairs);
  while (j - i > 1) {
    int k = (i + j) / 2;
    if (c == pairs[k].from)
      return pairs[k].to;
    if (c < pairs[k].from)
      j = k;
    else
      i = k;
  }

  /* check Box Drawing (U+2500-U+257F) and Block Elements (U+2580-U+259F)
  ┌ ┍ ┎ ┏ └ ┕ ┖ ┗ ├ ┝ ┞ ┟ ┠ ┡ ┢ ┣ ┭ ┱ ┵ ┹ ┽ ╃ ╅ ╉
  ┐ ┑ ┒ ┓ ┘ ┙ ┚ ┛ ┤ ┥ ┦ ┧ ┨ ┩ ┪ ┫ ┮ ┲ ┶ ┺ ┾ ╄ ╆ ╊
  ╒ ╓ ╔ ╘ ╙ ╚ ╞ ╟ ╠ ╭ ╯ ╱ ╴ ╸ ╼
  ╕ ╖ ╗ ╛ ╜ ╝ ╡ ╢ ╣ ╮ ╰ ╲ ╶ ╺ ╾

  cannot handle quarter/eighth blocks (would need to be reversed too) ▉▊▋▍▎
  could handle one eighth blocks ▏▕ but would be inconsistent

  ▌ ▖ ▘ ▙ ▚ ▛
  ▐ ▗ ▝ ▟ ▞ ▜
  */
  static const struct { wchar from, to; } boxpairs[] = {
    {0x250C, 0x2510}, {0x250D, 0x2511}, {0x250E, 0x2512}, {0x250F, 0x2513},
    {0x2510, 0x250C}, {0x2511, 0x250D}, {0x2512, 0x250E}, {0x2513, 0x250F},
    {0x2514, 0x2518}, {0x2515, 0x2519}, {0x2516, 0x251A}, {0x2517, 0x251B},
    {0x2518, 0x2514}, {0x2519, 0x2515}, {0x251A, 0x2516}, {0x251B, 0x2517},
    {0x251C, 0x2524}, {0x251D, 0x2525}, {0x251E, 0x2526}, {0x251F, 0x2527},
    {0x2520, 0x2528}, {0x2521, 0x2529}, {0x2522, 0x252A}, {0x2523, 0x252B},
    {0x2524, 0x251C}, {0x2525, 0x251D}, {0x2526, 0x251E}, {0x2527, 0x251F},
    {0x2528, 0x2520}, {0x2529, 0x2521}, {0x252A, 0x2522}, {0x252B, 0x2523},
    {0x252D, 0x252E}, {0x252E, 0x252D}, {0x2531, 0x2532}, {0x2532, 0x2531},
    {0x2535, 0x2536}, {0x2536, 0x2535}, {0x2539, 0x253A}, {0x253A, 0x2539},
    {0x253D, 0x253E}, {0x253E, 0x253D}, {0x2543, 0x2544}, {0x2544, 0x2543},
    {0x2545, 0x2546}, {0x2546, 0x2545}, {0x2549, 0x254A}, {0x254A, 0x2549},
    {0x2552, 0x2555}, {0x2553, 0x2556}, {0x2554, 0x2557}, {0x2555, 0x2552},
    {0x2556, 0x2553}, {0x2557, 0x2554}, {0x2558, 0x255B}, {0x2559, 0x255C},
    {0x255A, 0x255D}, {0x255B, 0x2558}, {0x255C, 0x2559}, {0x255D, 0x255A},
    {0x255E, 0x2561}, {0x255F, 0x2562}, {0x2560, 0x2563}, {0x2561, 0x255E},
    {0x2562, 0x255F}, {0x2563, 0x2560}, {0x256D, 0x256E}, {0x256E, 0x256D},
    {0x256F, 0x2570}, {0x2570, 0x256F}, {0x2571, 0x2572}, {0x2572, 0x2571},
    {0x2574, 0x2576}, {0x2576, 0x2574}, {0x2578, 0x257A}, {0x257A, 0x2578},
    {0x257C, 0x257E}, {0x257E, 0x257C}, {0x258C, 0x2590}, {0x2590, 0x258C},
    {0x2596, 0x2597}, {0x2597, 0x2596}, {0x2598, 0x259D}, {0x2599, 0x259F},
    {0x259A, 0x259E}, {0x259B, 0x259C}, {0x259C, 0x259B}, {0x259D, 0x2598},
    {0x259E, 0x259A}, {0x259F, 0x2599},
  };

  if (!box_mirror)
    return c;

  i = -1;
  j = lengthof(boxpairs);
  while (j - i > 1) {
    int k = (i + j) / 2;
    if (c == boxpairs[k].from)
      return boxpairs[k].to;
    if (c < boxpairs[k].from)
      j = k;
    else
      i = k;
  }

  return c;
}


#ifdef TEST_BIDI
uchar bidi_levels[999];
# define debug_bidi
int do_trace_bidi = 0;
#endif

/*
 * The Main Bidi Function, and the only function that should
 * be used by the outside world.
 *
 * line: a buffer of size count containing text to apply
 * the Bidirectional algorithm to.
 */
int
do_bidi(bool autodir, int paragraphLevel, bool explicitRTL, bool box_mirror, 
        bidi_char * line, int count)
{
  uchar currentEmbedding;
  uchar currentOverride;
  uchar tempType;
  int i, j;

  uchar bidi_class_of(int i) {
    ucschar c = line[i].wc;

#ifdef try_to_handle_CJK_here
#warning does not always work in RTL mode, now filtered before calling do_bidi
    if (i && c == UCSWIDE) {
      // try to fix double-width within right-to-left
      if (currentEmbedding & 1)
        i--;
      else
        return BN;
      // OK for LTR: return BN
      // OK for RTL U+5555: EN, NSM
      // not displayed in RTL: U+FF1C (class default -> ON)
    }
#endif

#ifdef check_emoji
    if (c == 0x200D
     || (c >= 0x2600 && c < 0x2800)
     || (c >= 0x1F300 && c < 0x20000)
       )
     return EN;
#endif
#ifdef check_emojilen
    if (line[i].emojilen)
      return EN;
#endif

    if (explicitRTL)
      return R;

    return bidi_class(c);
  }

 /* Rule (P1)  NOT IMPLEMENTED
  * P1. Split the text into separate paragraphs. A paragraph separator is
  * kept with the previous paragraph. Within each paragraph, apply all the
  * other rules of this algorithm.
  */

 /* Rule (P2), (P3)
  * P2. In each paragraph, find the first character of type L, AL, or R 
    while skipping over any characters between an isolate initiator and 
    its matching PDI or, if it has no matching PDI, the end of the paragraph.
  * P3. If a character is found in P2 and it is of type AL or R, then set
  * the paragraph embedding level to one; otherwise, set it to zero.
  */
  int isolateLevel = 0;
  int resLevel = -1;
  bool hasRTL = false;
  for (i = 0; i < count; i++) {
    int type = bidi_class_of(i);
    if (type == LRI || type == RLI || type == FSI) {
      hasRTL = true;
      isolateLevel++;
    }
    else if (type == PDI) {
      hasRTL = true;
      isolateLevel--;
    }
    else if (isolateLevel == 0) {
      if (type == R || type == AL) {
        hasRTL = true;
        if (resLevel < 0)
          resLevel = 1;
        break;
      }
      else if (type == RLE || type == LRE || type == RLO || type == LRO || type == PDF) {
        hasRTL = true;
        if (resLevel >= 0)
          break;
      }
      else if (type == L) {
        if (resLevel < 0)
          resLevel = 0;
      }
      else if (type == AN)
        hasRTL = true;
    }
  }
  if (autodir) {
    if (resLevel >= 0)
      paragraphLevel = resLevel;
  }
  else
    resLevel = paragraphLevel;
 /* Optimization: skip full algorithm if there is nothing to reorder */
  if (!hasRTL && !paragraphLevel)
    return 0;


 /* Initialize types, levels */
  uchar types[count];
  uchar levels[count];
  // workaround for gcc 11 warning anomaly
  types[0] = 0;

#define dont_debug_bidi

#ifdef debug_bidi
  uchar prev_types[count];
  uchar prev_levels[count];

  void trace_bidi(char * tag)
  {
#ifndef TEST_BIDI
    static int do_trace_bidi = 2;
#endif
    if (do_trace_bidi) {
      if (!tag) {
        do_trace_bidi--;
        return;
      }

      uint last = count - 1;
      while (last && line[last].wc == ' ')
        last--;

      bool vacuous = true;
      for (uint i = 0; i <= last; i++)
        if (types[i] != prev_types[i] || levels[i] != prev_levels[i]) {
          vacuous = false;
          break;
        }
      for (uint i = 0; i <= last; i++) {
        prev_types[i] = types[i];
        prev_levels[i] = levels[i];
      }
      if (vacuous)
        return;

      printf("%s\n", tag);
      for (uint i = 0; i <= last; i++)
        printf(" %04X", line[i].wc);
      printf("\n");
      static char * _type[] = {
        "  L", "LRE", "LRO", "  R", " AL", "RLE", "RLO", "PDF", " EN", " ES", " ET", " AN", " CS", "NSM", " BN", "  B", "  S", " WS", " ON", "LRI", "RLI", "FSI", "PDI"
      };
      for (uint i = 0; i <= last; i++)
        if (types[i] < lengthof(_type))
          printf("  %s", _type[types[i]]);
        else
          printf("    ?");
      printf("\n");
      for (uint i = 0; i <= last; i++)
          printf(" %4d", levels[i]);
      printf("\n");
    }
  }
  void trace_mark(char * tag) {
    (void)tag;
  }
  trace_bidi(0);
  for (i = 0; i < count; i++) {
    types[i] = ON;
    levels[i] = -1;
  }
#else
#define trace_bidi(tag)	
#define trace_mark(tag)	
#endif

  trace_bidi("[P2, P3]");

 /* Rule (X1)
    X1. At the beginning of a paragraph, perform the following steps:
  • Set the stack to empty.
  • Push onto the stack an entry consisting of the paragraph embedding level,
    a neutral directional override status, and a false directional isolate status.
  • Set the overflow isolate count to zero.
  • Set the overflow embedding count to zero.
  • Set the valid isolate count to zero.
  • Process each character iteratively, applying rules X2 through X8. 
    Only embedding levels from 0 through max_depth are valid in this phase. 
    (Note that in the resolution of levels in rules I1 and I2, 
    the maximum embedding level of max_depth+1 can be reached.)
  */
  currentEmbedding = paragraphLevel;
  currentOverride = ON;
  bool currentIsolate = false;

  // By making the dss as large as the whole line, we avoid overflow handling.
  uchar dss_emb[count + 1];
  uchar dss_ovr[count + 1];
  bool dss_isol[count + 1];
  int dss_top = -1;

  int countdss() { return dss_top + 1; }

  void pushdss() {
    dss_top++;
    dss_emb[dss_top] = currentEmbedding;
    dss_ovr[dss_top] = currentOverride;
    dss_isol[dss_top] = currentIsolate;
  }

  void popdss() {
    // remove top
    if (dss_top >= 0)
      dss_top--;
    // then set current values to new top
    if (dss_top >= 0) {
      currentEmbedding = dss_emb[dss_top];
      currentOverride = dss_ovr[dss_top];
      currentIsolate = dss_isol[dss_top];
    }
  }

  pushdss();
  //int ovfIsolate = 0;
  //int ovfEmbedding = 0;
  isolateLevel = 0;

 /* Rule (X2), (X3), (X4), (X5), (X6), (X7), (X8)
  * X2. With each RLE, compute the least greater odd embedding level.
  * X3. With each LRE, compute the least greater even embedding level.
  * X4. With each RLO, compute the least greater odd embedding level.
  * X5. With each LRO, compute the least greater even embedding level.
  * X6. For all types besides RLE, LRE, RLO, LRO, and PDF:
  *          a. Set the level of the current character to the current
  *              embedding level.
  *          b. Whenever the directional override status is not neutral,
  *              reset the current character type to the directional
  *              override status.
  * X7. With each PDF, determine the matching embedding or override code.
  * If there was a valid matching code, restore (pop) the last
  * remembered (pushed) embedding level and directional override.
  * X8. All explicit directional embeddings and overrides are completely
  * terminated at the end of each paragraph. Paragraph separators are not
  * included in the embedding. (Useless here) NOT IMPLEMENTED
  */
  for (i = 0; i < count; i++) {
    tempType = bidi_class_of(i);
    levels[i] = currentEmbedding;

    if (tempType == FSI) {
      int lvl = 0;
      tempType = LRI;
      for (int k = i + 1; k < count; k++) {
        uchar kType = bidi_class_of(k);
        if (kType == FSI || kType == RLI || kType == LRI)
          lvl++;
        else if (kType == PDI) {
          if (lvl)
            lvl--;
          else
            break;
        }
        else if (kType == R || kType == AL) {
          tempType = RLI;
          break;
        }
        else if (kType == L)
          break;
      }
    }
    switch (tempType) {
      when RLE:
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        currentOverride = ON;
        currentIsolate = false;
        pushdss();
        trace_mark("RLE");
      when LRE:
        currentEmbedding = leastGreaterEven(currentEmbedding);
        currentOverride = ON;
        currentIsolate = false;
        pushdss();
        trace_mark("LRE");
      when RLO:
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        currentOverride = R;
        currentIsolate = false;
        pushdss();
        trace_mark("RLO");
      when LRO:
        currentEmbedding = leastGreaterEven(currentEmbedding);
        currentOverride = L;
        currentIsolate = false;
        pushdss();
        trace_mark("LRO");
      when RLI:
        if (currentOverride != ON)
          tempType = currentOverride;
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        isolateLevel++;
        currentOverride = ON;
        currentIsolate = true;
        pushdss();
        trace_mark("RLI");
      when LRI:
        if (currentOverride != ON)
          tempType = currentOverride;
        currentEmbedding = leastGreaterEven(currentEmbedding);
        isolateLevel++;
        currentOverride = ON;
        currentIsolate = true;
        pushdss();
        trace_mark("LRI");
      when PDF:
        if (!currentIsolate && countdss() >= 2)
          popdss();
        levels[i] = currentEmbedding;
        trace_mark("PDF");
      when PDI:
        if (isolateLevel) {
          while (!currentIsolate && countdss() > 0)
            popdss();
          popdss();
          isolateLevel--;
        }
        if (currentOverride != ON)
          tempType = currentOverride;
        levels[i] = currentEmbedding;
        trace_mark("PDI");
      when WS or S: /* Whitespace is treated as neutral for now */
        if (currentOverride != ON)
          tempType = currentOverride;
      otherwise:
        if (currentOverride != ON)
          tempType = currentOverride;
    }
    types[i] = tempType;
  }
  trace_bidi("[X1-X8]");

 /* Rule (X9)
  * X9. Remove all RLE, LRE, RLO, LRO, PDF, and BN codes.
  * Here, they're converted to NSM (used to be BN).
  */
  bool skip[count];
  for (i = 0; i < count; i++) {
    switch (types[i]) {
      when RLE or LRE or RLO or LRO or PDF or BN:
        //types[i] = BN;
        types[i] = NSM;  // fixes 4594 test cases + 28 char test cases
        skip[i] = true;  // remove char from algorithm... (usage incomplete)
      otherwise:
        skip[i] = false;
    }
  }
  trace_bidi("[X9]");

 /* Rule (X10) NOT IMPLEMENTED
  * X10. Handle isolating run sequences...
  */

 /* Rule (W1)
  * W1. Examine each non-spacing mark (NSM) in the level run, and change
  * the type of the NSM to the type of the previous character. If the NSM
  * is at the start of the level run, it will get the type of sor.
  // TODO: check
    W1. Examine each nonspacing mark (NSM) in the isolating run sequence, 
    and change the type of the NSM 
    to Other Neutral if the previous character is an isolate initiator or PDI, 
    and to the type of the previous character otherwise. 
    If the NSM is at the start of the isolating run sequence, 
    it will get the type of sos. 
    (Note that in an isolating run sequence, an isolate initiator followed by 
    an NSM or any type other than PDI must be an overflow isolate initiator.)
  */
  if (types[0] == NSM /*&& !skip[0]*/)
    types[0] = (paragraphLevel & 1) ? R : L;  // sor

  for (i = 1; i < count; i++) {
    if (types[i] == NSM /*&& !skip[i]*/)
      switch (types[i - 1]) {
        when LRI or RLI or FSI or PDI:
          types[i] = ON;
        otherwise:
          types[i] = types[i - 1];
      }
  }
  trace_bidi("[W1]");

 /* Rule (W2)
  * W2. Search backwards from each instance of a European number until the
  * first strong type (R, L, AL, or sos) is found.  If an AL is found,
  * change the type of the European number to Arabic number.
  */
  for (i = 0; i < count; i++) {
    if (types[i] == EN) {
      j = i;
      while (j >= 0) {
        if (types[j] == AL) {
          types[i] = AN;
          break;
        }
        else if (types[j] == R || types[j] == L) {
          break;
        }
        j--;
      }
    }
  }
  trace_bidi("[W2]");

 /* Rule (W3)
  * W3. Change all ALs to R.
  *
  * Optimization: on Rule Xn, we might set a flag on AL type
  * to prevent this loop in L R lines only...
  */
  for (i = 0; i < count; i++) {
    if (types[i] == AL)
      types[i] = R;
  }
  trace_bidi("[W3]");

 /* Rule (W4)
  * W4. A single European separator between two European numbers changes
  * to a European number. A single common separator between two numbers
  * of the same type changes to that type.
  */
  for (i = 1; i < count - 1; i++) {
    if (types[i] == ES) {
      if (types[i - 1] == EN && types[i + 1] == EN)
        types[i] = EN;
    }
    else if (types[i] == CS) {
      if (types[i - 1] == EN && types[i + 1] == EN)
        types[i] = EN;
      else if (types[i - 1] == AN && types[i + 1] == AN)
        types[i] = AN;
    }
  }

 /* Rule (W5)
  * W5. A sequence of European terminators adjacent to European numbers
  * changes to all European numbers.
  *
  * Optimization: lots here... else ifs need rearrangement
  */
  for (i = 0; i < count; i++) {
    if (types[i] == ET) {
      if (i > 0 && types[i - 1] == EN) {
        types[i] = EN;
        continue;
      }
      else if (i < count - 1 && types[i + 1] == EN) {
        types[i] = EN;
        continue;
      }
      else if (i < count - 1 && types[i + 1] == ET) {
        j = i;
        while (j < count && types[j] == ET) {
          j++;
        }
        if (types[j] == EN)
          types[i] = EN;
      }
    }
  }

 /* Rule (W6)
  * W6. Otherwise, separators and terminators change to Other Neutral:
  */
  for (i = 0; i < count; i++) {
    switch (types[i]) {
      when ES or ET or CS:
        types[i] = ON;
    }
  }
  trace_bidi("[W4] [W5] [W6]");

#define dont_consider_BD13

#ifdef consider_BD13
  int isol_run_level;
#endif
  void clear_isol()
  {
#ifdef consider_BD13
    isol_run_level = 0;
#endif
  }
  bool break_isol(int j)
  {
    if (!j)
      return true;
#ifdef consider_BD13
    // BD13 describes "isolating run sequences" for use according to X10;
    // however, it defines only isolate marks (particularly PDI) as 
    // boundaries, inconsistently with some examples in BD13 and X10;
    // this whole attempt does not seem to make much difference anyway...
    // (-38 +18 BidiTest cases, all irregular / unsymmetric markers)
    if (types[j] == PDI) {
      isol_run_level++;
      return false;
    }
    else if (bidi_class_of(j) == RLI || bidi_class_of(j) == LRI || bidi_class_of(j) == FSI) {
      if (isol_run_level)
        isol_run_level--;
      return false;
    }
    else if (isol_run_level)
      return false;
# ifdef consider_X10_Example_1
    // does not make a difference
    else if (bidi_class_of(j - 1) == PDF && (bidi_class_of(j) == RLE || bidi_class_of(j) == LRE)) {
      // X10 Example 1
      return false;
    }
# endif
    else {
      j--;
      if (bidi_class_of(j) == RLE || bidi_class_of(j) == LRE)
        return true;
# ifdef break_at_PDF
      if (bidi_class_of(j) == PDF)
        return true;
# endif
    }
#endif
    return false;
  }

 /* Rule (W7)
  * W7. Search backwards from each instance of a European number until
  * the first strong type (R, L, or sor) is found. If an L is found,
  * then change the type of the European number to L.
  */
  int embeddingLevel = paragraphLevel;  //TODO: sor
  for (i = 0; i < count; i++) {
    if (types[i] == EN) {
      j = i;
      clear_isol();
      while (j >= 0) {
        if (types[j] == L) {
          types[i] = L;
          break;
        }
        else if (types[j] == R || types[j] == AL) {
          break;
        }
        if (break_isol(j)) {
          // nothing found, fallback to sor
          if (!(embeddingLevel & 1))
            types[i] = L;
          break;
        }
        j--;
      }
    }
  }
  trace_bidi("[W7]");

 /* Rule (N0) Handle bracket pairs in isolating run sequences.
  * N0. Process bracket pairs in an isolating run sequence 
    sequentially in the logical order of the text positions of the 
    opening paired brackets using the logic given below.
    Within this scope, bidirectional types EN and AN are treated as R.
  */
#define is_N0_R(type) (type == R || type == EN || type == AN)
  // refer to embedding level
  embeddingLevel = paragraphLevel;  //TODO: to become sos when we handle isolating runs
  // create stack and list of bracket pairs
  struct {
    ucschar b;
    int o;
  } brackstack[count];
  int top = 0;
  struct {
    int o, c;
  } brackpairs[count];
  int bracks = 0;
  // look for bracket pairs
  // • Identify the bracket pairs in the current isolating run sequence.
  for (i = 0; i < count; i++) {
    ucschar bro = bracket(line[i].wc);
    if (bro == line[i].wc) {  // opening bracket
      brackstack[top].b = canonical(bro);
      brackstack[top].o = i;
      //printf("N0 > bracket %d ...\n", i);
      top++;
    }
    else if (bro) {  // closing bracket, bro is the opening bracket
      int sp = top;
      while (sp) {
        sp--;
        // compare canonical equivalents, factoring out 
        // SUPERSCRIPT/SUBSCRIPT/SMALL/FULLWIDTH/HALFWIDTH
        if (canonical(bro) == brackstack[sp].b) {
          // insert bracket pair into sorted list
          int bracki = bracks;
          while (bracki && brackpairs[bracki - 1].o > brackstack[sp].o) {
            brackpairs[bracki] = brackpairs[bracki - 1];
            bracki--;
          }
          brackpairs[bracki].o = brackstack[sp].o;
          brackpairs[bracki].c = i;
          //printf("N0 > bracket %d %d\n", brackstack[sp].o, i);
          bracks++;
          top = sp;
          break;
        }
      }
    }
  }
#define dont_debug_N0
  // now handle the determined bracket pairs, assuming they are sorted
  // • For each bracket-pair element in the list of pairs of text positions
  for (int k = 0; k < bracks; k++) {
    int o = brackpairs[k].o;
    int c = brackpairs[k].c;
#ifdef debug_N0
    printf("N0 < bracket %d %d\n", o, c);
#endif
    bool otherstrong = false;
    // a. Inspect the bidirectional types of the characters enclosed within the bracket pair.
    for (int j = o + 1; j < c; j++) {
      uchar type = types[j];
      // b. If any strong type (either L or R) matching the embedding 
      // direction is found, set the type for both brackets in the pair 
      // to match the embedding direction.
      if ((type == L && !(embeddingLevel & 1)) || (is_N0_R(type) && (embeddingLevel & 1))) {
#ifdef debug_N0
        printf("N0 strong emb %d\n", embeddingLevel);
#endif
        types[o] = types[c] = (embeddingLevel & 1) ? R : L;
        otherstrong = false;
        break;
      }
      // c. Otherwise, if there is a strong type it must be 
      // opposite the embedding direction.
      else if (type == L || is_N0_R(type)) {
        otherstrong = true;
      }
    }
    if (otherstrong) {
      // c. Otherwise, ...
      // Therefore, test for an established context with a preceding 
      // strong type by checking backwards before the opening paired 
      // bracket until the first strong type (L, R, or sos) is found.
      int precedingLevel = embeddingLevel;
      for (int j = o - 1; j >= 0; j--)
        if (types[j] == L || is_N0_R(types[j])) {
          precedingLevel = (embeddingLevel & ~1) + (types[j] != L);
          //printf("N0 strong other %d preceding @%d\n", embeddingLevel, j);
          break;
        }
#ifdef debug_N0
      printf("N0 strong other %d preceding %d\n", embeddingLevel, precedingLevel);
#endif
      // 1. If the preceding strong type is also opposite the 
      // embedding direction, context is established, so set the type for 
      // both brackets in the pair to that direction.
      if ((precedingLevel & 1) != (embeddingLevel & 1))
        types[o] = types[c] = (precedingLevel & 1) ? R : L;
      // 2. Otherwise set the type for both brackets in the pair to 
      // the embedding direction.
      else
        types[o] = types[c] = (embeddingLevel & 1) ? R : L;
    }
    // d. Otherwise, there are no strong types within the bracket pair. 
    // Therefore, do not set the type for that bracket pair.
    // • Any number of characters that had original bidirectional 
    // character type NSM prior to the application of W1 that immediately 
    // follow a paired bracket which changed to L or R under N0 should 
    // change to match the type of their preceding bracket.
    if (types[c] == L || is_N0_R(types[c]))
      for (int j = c + 1; j < count; j++) {
        if (bidi_class_of(j) == NSM /*&& !skip[j]*/)
          types[j] = types[c];
        else
          break;
      }
  }
  trace_bidi("[N0]");

 /* Rule (N1)
  * N1. A sequence of NIs takes the direction of the surrounding
  * strong text if the text on both sides has the same direction.
  * European and Arabic numbers are treated as though they were R.
  // TODO: check
    European and Arabic numbers act as if they were R in terms of their 
    influence on NIs. The start-of-sequence (sos) and end-of-sequence (eos) 
    types are used at isolating run sequence boundaries.
  */
  if (count >= 2 && is_NI(types[0])) {
    if ((paragraphLevel & 1) &&
        ((types[1] == R) || (types[1] == EN) || (types[1] == AN))
       )
      types[0] = R;
    else if (!(paragraphLevel & 1) && types[1] == L)
      types[0] = L;
  }
  for (i = 1; i < count - 1; i++) {
    if (is_NI(types[i])) {
      if (types[i - 1] == L) {
        j = i;
        while (j < count - 1 && is_NI(types[j])) {
          j++;
        }
        if (types[j] == L) {
          while (i < j) {
            types[i] = L;
            i++;
          }
        }

      }
      else if ((types[i - 1] == R) || (types[i - 1] == EN) ||
               (types[i - 1] == AN)) {
        j = i;
        while (j < count - 1 && is_NI(types[j])) {
          j++;
        }
        if ((types[j] == R) || (types[j] == EN) || (types[j] == AN)) {
          while (i < j) {
            types[i] = R;
            i++;
          }
        }
      }
    }
  }
  if (count >= 2 && is_NI(types[count - 1])) {
    if ((paragraphLevel & 1) &&
        (types[count - 2] == R || types[count - 2] == EN || types[count - 2] == AN)
       )
      types[count - 1] = R;
    else if (!(paragraphLevel & 1) && types[count - 2] == L)
      types[count - 1] = L;
  }
  trace_bidi("[N1]");

 /* Rule (N2)
  * N2. Any remaining NIs take the embedding direction.
  */
  for (i = 0; i < count; i++) {
    if (is_NI(types[i])) {
      if ((levels[i] % 2) == 0)
        types[i] = L;
      else
        types[i] = R;
    }
  }
  trace_bidi("[N2]");

 /* Rules (I1) (I2)
  * I1. For all characters with an even (left-to-right) embedding
  * direction, those of type R go up one level and those of type AN or
  * EN go up two levels.
  * I2. For all characters with an odd (right-to-left) embedding direction,
  * those of type L, EN or AN go up one level.
  */
  for (i = 0; i < count; i++) {
    if ((levels[i] % 2) == 0) {
      // (I1)
      if (types[i] == R)
        levels[i] += 1;
      else if (types[i] == AN || types[i] == EN)
        levels[i] += 2;
    }
    else
      // (I2)
      if (types[i] == L || types[i] == EN || types[i] == AN)
        levels[i] += 1;
  }
  trace_bidi("[I1, I2]");

 /* Rule (L1)
  * L1. On each line, reset the embedding level of the following characters
  * to the paragraph embedding level:
  *   (1) segment separators,
  *   (2) paragraph separators,
  *   (3) any sequence of whitespace characters or isolate markers
  *       preceding a segment separator or paragraph separator,
  *   (4) and any sequence of whitespace characters or isolate markers
  *       at the end of the line.
  * The types of characters used here are the original types, not those
  * modified by the previous phase.
    N/A: Because a paragraph separator breaks lines, there will be at most 
    one per line, at the end of that line.
  */
  // (4)
  j = count - 1;
  while (j > 0 && (bidi_class_of(j) == WS || skip[j])) {
    j--;
  }
  if (j < count - 1) {
    for (j++; j < count; j++)
      levels[j] = paragraphLevel;
  }
  // (3)
  for (i = 0; i < count; i++) {
    tempType = bidi_class_of(i);
#ifdef check_emojiseq
    if (line[i].emojilen) {
      // raise emoji sequence to LTR level, as a rough approximation of
      // HL3. Emulate explicit directional formatting characters.
      for (j = i; j < i + line[i].emojilen && j < count && line[j].emojilen; j++) {
        levels[j] = leastGreaterEven(levels[i]);
      }
    }
    else
#endif
    if (tempType == WS) {
      j = i;
      while (j < count && (bidi_class_of(j) == WS || skip[j])) {
        j++;
      }
      if (j == count || bidi_class_of(j) == B || bidi_class_of(j) == S) {
        for (j--; j >= i; j--) {
          levels[j] = paragraphLevel;
        }
      }
    }
    else if (tempType == B || tempType == S) {
      levels[i] = paragraphLevel;
    }
  }

#ifdef TEST_BIDI
  memcpy(bidi_levels, levels, count);
#endif

 /* Rule (L4)
  * L4. A character that possesses the mirrored property as specified by
  * Section 4.7, Mirrored, must be depicted by a mirrored glyph if the
  * resolved directionality of that character is R.
  */
 /* Note: this is implemented before L2 for efficiency */
  for (i = 0; i < count; i++)
    if ((levels[i] % 2) == 1)
      line[i].wc = mirror(line[i].wc, box_mirror);

 /* Rule (L2)
  * L2. From the highest level found in the text to the lowest odd level on
  * each line, including intermediate levels not actually present in the
  * text, reverse any contiguous sequence of characters that are at that
  * level or higher
  */
 /* we flip the character string and leave the level array */
  i = 0;
  tempType = levels[0];
  while (i < count) {
    if (levels[i] > tempType)
      tempType = levels[i];
    i++;
  }
 /* maximum level in tempType. */
  while (tempType > 0) { /* loop from highest level to the least odd */
    flip_runs(line, levels, tempType, count);
    tempType--;
  }
  trace_bidi("[L1] [L4] [L2]");

 /* Rule (L3) NOT IMPLEMENTED
  * L3. Combining marks applied to a right-to-left base character will at
  * this point precede their base character. If the rendering engine
  * expects them to follow the base characters in the final display
  * process, then the ordering of the marks and the base character must
  * be reversed.
  */
  // This is not relevant for mintty as the combining characters are kept 
  // hidden from this algorithm and are maintained transparently to it.

  return resLevel;
}
