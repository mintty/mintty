#include "minibidi.h"

/************************************************************************
 * $Id: minibidi.c 6910 2006-11-18 15:10:48Z simon $
 *
 * ------------
 * Description:
 * ------------
 * This is an implemention of Unicode's Bidirectional Algorithm
 * (known as UAX #9).
 *
 *   http://www.unicode.org/reports/tr9/
 *
 * Author: Ahmad Khalifa
 * (www.arabeyes.org - under MIT license)
 *
 * Modified: Thomas Wolff:
 * - extended bidi class detection to consider non-BMP characters
 * - fixed mirroring of '('
 *
 ************************************************************************/

/*
 * TODO:
 * =====
 * - Explicit marks need to be handled (they are not 100% now)
 * - Ligatures
 */


#define LMASK	0x3F    /* Embedding Level mask */
#define OMASK	0xC0    /* Override mask */
#define OISL	0x80    /* Override is L */
#define OISR	0x40    /* Override is R */

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
 * Finds the index of a run with level equals tlevel
 */
static int
findIndexOfRun(uchar * level, int start, int count, int tlevel)
{
  int i;
  for (i = start; i < count; i++) {
    if (tlevel == level[i]) {
      return i;
    }
  }
  return count;
}

/*
 * Flips the text buffer, according to max level, and
 * all higher levels
 *
 * Input:
 * from: text buffer, on which to apply flipping
 * level: resolved levels buffer
 * max: the maximum level found in this line (should be uchar)
 * count: line size in bidi_char
 */
static void
flipThisRun(bidi_char * from, uchar * level, int max, int count)
{
  int i, j, k, tlevel;
  bidi_char temp;

  j = i = 0;
  while (i < count && j < count) {
   /* find the start of the run of level=max */
    tlevel = max;
    i = j = findIndexOfRun(level, i, count, max);
   /* find the end of the run */
    while (i < count && tlevel <= level[i]) {
      i++;
    }
    for (k = i - 1; k > j; k--, j++) {
      temp = from[k];
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
  const int mask = (1 << R) | (1 << AL) | (1 << RLE) | (1 << RLO);

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

static ucschar
mirror(ucschar c)
{
  static const struct { wchar from, to; } pairs[] = {
    {0x0028, 0x0029}, {0x0029, 0x0028}, {0x003C, 0x003E}, {0x003E, 0x003C},
    {0x005B, 0x005D}, {0x005D, 0x005B}, {0x007B, 0x007D}, {0x007D, 0x007B},
    {0x00AB, 0x00BB}, {0x00BB, 0x00AB}, {0x2039, 0x203A}, {0x203A, 0x2039},
    {0x2045, 0x2046}, {0x2046, 0x2045}, {0x207D, 0x207E}, {0x207E, 0x207D},
    {0x208D, 0x208E}, {0x208E, 0x208D}, {0x2208, 0x220B}, {0x2209, 0x220C},
    {0x220A, 0x220D}, {0x220B, 0x2208}, {0x220C, 0x2209}, {0x220D, 0x220A},
    {0x2215, 0x29F5}, {0x223C, 0x223D}, {0x223D, 0x223C}, {0x2243, 0x22CD},
    {0x2252, 0x2253}, {0x2253, 0x2252}, {0x2254, 0x2255}, {0x2255, 0x2254},
    {0x2264, 0x2265}, {0x2265, 0x2264}, {0x2266, 0x2267}, {0x2267, 0x2266},
    {0x2268, 0x2269}, {0x2269, 0x2268}, {0x226A, 0x226B}, {0x226B, 0x226A},
    {0x226E, 0x226F}, {0x226F, 0x226E}, {0x2270, 0x2271}, {0x2271, 0x2270},
    {0x2272, 0x2273}, {0x2273, 0x2272}, {0x2274, 0x2275}, {0x2275, 0x2274},
    {0x2276, 0x2277}, {0x2277, 0x2276}, {0x2278, 0x2279}, {0x2279, 0x2278},
    {0x227A, 0x227B}, {0x227B, 0x227A}, {0x227C, 0x227D}, {0x227D, 0x227C},
    {0x227E, 0x227F}, {0x227F, 0x227E}, {0x2280, 0x2281}, {0x2281, 0x2280},
    {0x2282, 0x2283}, {0x2283, 0x2282}, {0x2284, 0x2285}, {0x2285, 0x2284},
    {0x2286, 0x2287}, {0x2287, 0x2286}, {0x2288, 0x2289}, {0x2289, 0x2288},
    {0x228A, 0x228B}, {0x228B, 0x228A}, {0x228F, 0x2290}, {0x2290, 0x228F},
    {0x2291, 0x2292}, {0x2292, 0x2291}, {0x2298, 0x29B8}, {0x22A2, 0x22A3},
    {0x22A3, 0x22A2}, {0x22A6, 0x2ADE}, {0x22A8, 0x2AE4}, {0x22A9, 0x2AE3},
    {0x22AB, 0x2AE5}, {0x22B0, 0x22B1}, {0x22B1, 0x22B0}, {0x22B2, 0x22B3},
    {0x22B3, 0x22B2}, {0x22B4, 0x22B5}, {0x22B5, 0x22B4}, {0x22B6, 0x22B7},
    {0x22B7, 0x22B6}, {0x22C9, 0x22CA}, {0x22CA, 0x22C9}, {0x22CB, 0x22CC},
    {0x22CC, 0x22CB}, {0x22CD, 0x2243}, {0x22D0, 0x22D1}, {0x22D1, 0x22D0},
    {0x22D6, 0x22D7}, {0x22D7, 0x22D6}, {0x22D8, 0x22D9}, {0x22D9, 0x22D8},
    {0x22DA, 0x22DB}, {0x22DB, 0x22DA}, {0x22DC, 0x22DD}, {0x22DD, 0x22DC},
    {0x22DE, 0x22DF}, {0x22DF, 0x22DE}, {0x22E0, 0x22E1}, {0x22E1, 0x22E0},
    {0x22E2, 0x22E3}, {0x22E3, 0x22E2}, {0x22E4, 0x22E5}, {0x22E5, 0x22E4},
    {0x22E6, 0x22E7}, {0x22E7, 0x22E6}, {0x22E8, 0x22E9}, {0x22E9, 0x22E8},
    {0x22EA, 0x22EB}, {0x22EB, 0x22EA}, {0x22EC, 0x22ED}, {0x22ED, 0x22EC},
    {0x22F0, 0x22F1}, {0x22F1, 0x22F0}, {0x22F2, 0x22FA}, {0x22F3, 0x22FB},
    {0x22F4, 0x22FC}, {0x22F6, 0x22FD}, {0x22F7, 0x22FE}, {0x22FA, 0x22F2},
    {0x22FB, 0x22F3}, {0x22FC, 0x22F4}, {0x22FD, 0x22F6}, {0x22FE, 0x22F7},
    {0x2308, 0x2309}, {0x2309, 0x2308}, {0x230A, 0x230B}, {0x230B, 0x230A},
    {0x2329, 0x232A}, {0x232A, 0x2329}, {0x2768, 0x2769}, {0x2769, 0x2768},
    {0x276A, 0x276B}, {0x276B, 0x276A}, {0x276C, 0x276D}, {0x276D, 0x276C},
    {0x276E, 0x276F}, {0x276F, 0x276E}, {0x2770, 0x2771}, {0x2771, 0x2770},
    {0x2772, 0x2773}, {0x2773, 0x2772}, {0x2774, 0x2775}, {0x2775, 0x2774},
    {0x27D5, 0x27D6}, {0x27D6, 0x27D5}, {0x27DD, 0x27DE}, {0x27DE, 0x27DD},
    {0x27E2, 0x27E3}, {0x27E3, 0x27E2}, {0x27E4, 0x27E5}, {0x27E5, 0x27E4},
    {0x27E6, 0x27E7}, {0x27E7, 0x27E6}, {0x27E8, 0x27E9}, {0x27E9, 0x27E8},
    {0x27EA, 0x27EB}, {0x27EB, 0x27EA}, {0x2983, 0x2984}, {0x2984, 0x2983},
    {0x2985, 0x2986}, {0x2986, 0x2985}, {0x2987, 0x2988}, {0x2988, 0x2987},
    {0x2989, 0x298A}, {0x298A, 0x2989}, {0x298B, 0x298C}, {0x298C, 0x298B},
    {0x298D, 0x2990}, {0x298E, 0x298F}, {0x298F, 0x298E}, {0x2990, 0x298D},
    {0x2991, 0x2992}, {0x2992, 0x2991}, {0x2993, 0x2994}, {0x2994, 0x2993},
    {0x2995, 0x2996}, {0x2996, 0x2995}, {0x2997, 0x2998}, {0x2998, 0x2997},
    {0x29B8, 0x2298}, {0x29C0, 0x29C1}, {0x29C1, 0x29C0}, {0x29C4, 0x29C5},
    {0x29C5, 0x29C4}, {0x29CF, 0x29D0}, {0x29D0, 0x29CF}, {0x29D1, 0x29D2},
    {0x29D2, 0x29D1}, {0x29D4, 0x29D5}, {0x29D5, 0x29D4}, {0x29D8, 0x29D9},
    {0x29D9, 0x29D8}, {0x29DA, 0x29DB}, {0x29DB, 0x29DA}, {0x29F5, 0x2215},
    {0x29F8, 0x29F9}, {0x29F9, 0x29F8}, {0x29FC, 0x29FD}, {0x29FD, 0x29FC},
    {0x2A2B, 0x2A2C}, {0x2A2C, 0x2A2B}, {0x2A2D, 0x2A2C}, {0x2A2E, 0x2A2D},
    {0x2A34, 0x2A35}, {0x2A35, 0x2A34}, {0x2A3C, 0x2A3D}, {0x2A3D, 0x2A3C},
    {0x2A64, 0x2A65}, {0x2A65, 0x2A64}, {0x2A79, 0x2A7A}, {0x2A7A, 0x2A79},
    {0x2A7D, 0x2A7E}, {0x2A7E, 0x2A7D}, {0x2A7F, 0x2A80}, {0x2A80, 0x2A7F},
    {0x2A81, 0x2A82}, {0x2A82, 0x2A81}, {0x2A83, 0x2A84}, {0x2A84, 0x2A83},
    {0x2A8B, 0x2A8C}, {0x2A8C, 0x2A8B}, {0x2A91, 0x2A92}, {0x2A92, 0x2A91},
    {0x2A93, 0x2A94}, {0x2A94, 0x2A93}, {0x2A95, 0x2A96}, {0x2A96, 0x2A95},
    {0x2A97, 0x2A98}, {0x2A98, 0x2A97}, {0x2A99, 0x2A9A}, {0x2A9A, 0x2A99},
    {0x2A9B, 0x2A9C}, {0x2A9C, 0x2A9B}, {0x2AA1, 0x2AA2}, {0x2AA2, 0x2AA1},
    {0x2AA6, 0x2AA7}, {0x2AA7, 0x2AA6}, {0x2AA8, 0x2AA9}, {0x2AA9, 0x2AA8},
    {0x2AAA, 0x2AAB}, {0x2AAB, 0x2AAA}, {0x2AAC, 0x2AAD}, {0x2AAD, 0x2AAC},
    {0x2AAF, 0x2AB0}, {0x2AB0, 0x2AAF}, {0x2AB3, 0x2AB4}, {0x2AB4, 0x2AB3},
    {0x2ABB, 0x2ABC}, {0x2ABC, 0x2ABB}, {0x2ABD, 0x2ABE}, {0x2ABE, 0x2ABD},
    {0x2ABF, 0x2AC0}, {0x2AC0, 0x2ABF}, {0x2AC1, 0x2AC2}, {0x2AC2, 0x2AC1},
    {0x2AC3, 0x2AC4}, {0x2AC4, 0x2AC3}, {0x2AC5, 0x2AC6}, {0x2AC6, 0x2AC5},
    {0x2ACD, 0x2ACE}, {0x2ACE, 0x2ACD}, {0x2ACF, 0x2AD0}, {0x2AD0, 0x2ACF},
    {0x2AD1, 0x2AD2}, {0x2AD2, 0x2AD1}, {0x2AD3, 0x2AD4}, {0x2AD4, 0x2AD3},
    {0x2AD5, 0x2AD6}, {0x2AD6, 0x2AD5}, {0x2ADE, 0x22A6}, {0x2AE3, 0x22A9},
    {0x2AE4, 0x22A8}, {0x2AE5, 0x22AB}, {0x2AEC, 0x2AED}, {0x2AED, 0x2AEC},
    {0x2AF7, 0x2AF8}, {0x2AF8, 0x2AF7}, {0x2AF9, 0x2AFA}, {0x2AFA, 0x2AF9},
    {0x3008, 0x3009}, {0x3009, 0x3008}, {0x300A, 0x300B}, {0x300B, 0x300A},
    {0x300C, 0x300D}, {0x300D, 0x300C}, {0x300E, 0x300F}, {0x300F, 0x300E},
    {0x3010, 0x3011}, {0x3011, 0x3010}, {0x3014, 0x3015}, {0x3015, 0x3014},
    {0x3016, 0x3017}, {0x3017, 0x3016}, {0x3018, 0x3019}, {0x3019, 0x3018},
    {0x301A, 0x301B}, {0x301B, 0x301A}, {0xFF08, 0xFF09}, {0xFF09, 0xFF08},
    {0xFF1C, 0xFF1E}, {0xFF1E, 0xFF1C}, {0xFF3B, 0xFF3D}, {0xFF3D, 0xFF3B},
    {0xFF5B, 0xFF5D}, {0xFF5D, 0xFF5B}, {0xFF5F, 0xFF60}, {0xFF60, 0xFF5F},
    {0xFF62, 0xFF63}, {0xFF63, 0xFF62}
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
  return c;
}

/*
 * The most significant 2 bits of each level are used to store
 * Override status of each character
 * This function sets the override bits of level according
 * to the value in override, and reurns the new byte.
 */
static uchar
setOverrideBits(uchar level, uchar override)
{
  if (override == ON)
    return level;
  else if (override == R)
    return level | OISR;
  else if (override == L)
    return level | OISL;
  return level;
}

/*
 * Find the most recent run of the same value in `level', and
 * return the value _before_ it.
 * Used to be used to process U+202C POP DIRECTIONAL FORMATTING.
 */
static int
getPreviousLevel(uchar * level, int from)
{
  if (from > 0) {
    uchar current = level[--from];

    while (from >= 0 && level[from] == current)
      from--;

    if (from >= 0)
      return level[from];

    return -1;
  }
  else
    return -1;
}

/*
 * The Main Bidi Function, and the only function that should
 * be used by the outside world.
 *
 * line: a buffer of size count containing text to apply
 * the Bidirectional algorithm to.
 */

int
do_bidi(bidi_char * line, int count)
{
  uchar paragraphLevel;
  uchar currentEmbedding;
  uchar currentOverride;
  uchar tempType;
  int i, j, yes, bover;

  uchar bidi_class_of(int i) { return bidi_class(line[i].wc); }

 /* Check the presence of R or AL types as optimization */
  yes = 0;
  for (i = 0; i < count; i++) {
    int type = bidi_class_of(i);
    if (type == R || type == AL
        || type == RLE || type == LRE || type == RLO || type == LRO || type == PDF
        || type == LRI || type == RLI || type == FSI || type == PDI
       ) {
      yes = 1;
      break;
    }
  }
  if (yes == 0)
    return L;

 /* Initialize types, levels */
  uchar types[count];
  uchar levels[count];

 /* Rule (P1)  NOT IMPLEMENTED
  * P1. Split the text into separate paragraphs. A paragraph separator is
  * kept with the previous paragraph. Within each paragraph, apply all the
  * other rules of this algorithm.
  */

 /* Rule (P2), (P3)
  * P2. In each paragraph, find the first character of type L, AL, or R.
  * P3. If a character is found in P2 and it is of type AL or R, then set
  * the paragraph embedding level to one; otherwise, set it to zero.
  */
  paragraphLevel = 0;
  int isolateLevel = 0;
  for (i = 0; i < count; i++) {
    int type = bidi_class_of(i);
    if (type == LRI || type == RLI || type == FSI)
      isolateLevel++;
    else if (type == PDI)
      isolateLevel--;
    else if (isolateLevel == 0) {
      if (type == R || type == AL) {
        paragraphLevel = 1;
        break;
      }
      else if (type == L)
        break;
    }
  }

 /* Rule (X1)
  * X1. Begin by setting the current embedding level to the paragraph
  * embedding level. Set the directional override status to neutral.
  */
  currentEmbedding = paragraphLevel;
  currentOverride = ON;
  bool currentIsolate = false;

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
    dss_top--;
    // then set current values to new top
    currentEmbedding = dss_emb[dss_top];
    currentOverride = dss_ovr[dss_top];
    currentIsolate = dss_isol[dss_top];
  }

  pushdss();
  //int ovfIsolate = 0;
  //int ovfEmbedding = 0;
  isolateLevel = 0;

#define dont_debug_bidi

#ifdef debug_bidi
  void trace_bidi(char * tag) {
    if ((*line).wc == 'x') {
      printf("%s ", tag);
      for (int i = 0; i < count; i++) {
        if (line[i].wc == ' ')
          printf(" ");
        else if (line[i].wc < 0x80)
          printf("%d:%c/%d ", levels[i], line[i].wc, types[i]);
        else
          printf("%d:%X/%d ", levels[i], line[i].wc, types[i]);
      }
      printf("\n");
    }
  }
  void trace_mark(char * tag) {
    (void)tag;
  }
#else
#define trace_bidi(tag)	
#define trace_mark(tag)	
#endif

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
  bover = 0;
  for (i = 0; i < count; i++) {
    tempType = bidi_class_of(i);
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
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        //levels[i] = currentEmbedding;
        levels[i] = setOverrideBits(levels[i], currentOverride);
        currentOverride = ON;
        currentIsolate = false;
        pushdss();
        trace_mark("RLE");
      when LRE:
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterEven(currentEmbedding);
        //levels[i] = currentEmbedding;
        levels[i] = setOverrideBits(levels[i], currentOverride);
        currentOverride = ON;
        currentIsolate = false;
        pushdss();
        trace_mark("LRE");
      when RLO:
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        //levels[i] = currentEmbedding;
        tempType = currentOverride = R;
        currentIsolate = false;
        pushdss();
        bover = 1;
        trace_mark("RLO");
      when LRO:
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterEven(currentEmbedding);
        //levels[i] = currentEmbedding;
        tempType = currentOverride = L;
        currentIsolate = false;
        pushdss();
        bover = 1;
        trace_mark("LRO");
      when RLI:
        if (currentOverride != ON)
          tempType = currentOverride;
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterOdd(currentEmbedding);
        //levels[i] = currentEmbedding;
        isolateLevel++;
        currentOverride = ON;
        currentIsolate = true;
        pushdss();
        trace_mark("RLI");
      when LRI:
        if (currentOverride != ON)
          tempType = currentOverride;
        levels[i] = currentEmbedding;
        currentEmbedding = leastGreaterEven(currentEmbedding);
        //levels[i] = currentEmbedding;
        isolateLevel++;
        currentOverride = ON;
        currentIsolate = true;
        pushdss();
        trace_mark("LRI");
      when PDF:
#ifdef old_PDF_handling
#warning old PDF handling does not work correctly for override
        if (getPreviousLevel(levels, i) == -1) {
          currentEmbedding = paragraphLevel;
          currentOverride = ON;
        }
        else {
          currentOverride = currentEmbedding & OMASK;
          currentEmbedding = currentEmbedding & ~OMASK;
        }
        levels[i] = currentEmbedding;
#else
        (void)getPreviousLevel;
        if (countdss() > 1 && !currentIsolate)
          popdss();
        levels[i] = currentEmbedding;
#endif
        trace_mark("PDF");
      when PDI:
        if (isolateLevel) {
          while (!currentIsolate)
            popdss();
          popdss();
          isolateLevel--;
        }
        if (currentOverride != ON)
          tempType = currentOverride;
        levels[i] = currentEmbedding;
        trace_mark("PDI");
      when WS or S: /* Whitespace is treated as neutral for now */
        levels[i] = currentEmbedding;
        tempType = ON;
        if (currentOverride != ON)
          tempType = currentOverride;
      otherwise:
        levels[i] = currentEmbedding;
        if (currentOverride != ON)
          tempType = currentOverride;
    }
    types[i] = tempType;
  }
 /* this clears out all overrides, so we can use levels safely... */
 /* checks bover first */
  if (bover)
    for (i = 0; i < count; i++)
      levels[i] = levels[i] & LMASK;
  trace_bidi("<X9");

 /* Rule (X9)
  * X9. Remove all RLE, LRE, RLO, LRO, PDF, and BN codes.
  * Here, they're converted to BN.
  */
  for (i = 0; i < count; i++) {
    switch (types[i]) {
      when RLE or LRE or RLO or LRO or PDF:
        types[i] = BN;
    }
  }

 /* Rule (W1)
  * W1. Examine each non-spacing mark (NSM) in the level run, and change
  * the type of the NSM to the type of the previous character. If the NSM
  * is at the start of the level run, it will get the type of sor.
  */
  if (types[0] == NSM)
    types[0] = paragraphLevel;

  for (i = 1; i < count; i++) {
    if (types[i] == NSM)
      types[i] = types[i - 1];
   /* Is this a safe assumption?
    * I assumed the previous, IS a character.
    */
  }

 /* Rule (W2)
  * W2. Search backwards from each instance of a European number until the
  * first strong type (R, L, AL, or sor) is found.  If an AL is found,
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

 /* Rule (W4)
  * W4. A single European separator between two European numbers changes
  * to a European number. A single common separator between two numbers
  * of the same type changes to that type.
  */
  for (i = 1; i < (count - 1); i++) {
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

 /* Rule (W7)
  * W7. Search backwards from each instance of a European number until
  * the first strong type (R, L, or sor) is found. If an L is found,
  * then change the type of the European number to L.
  */
  for (i = 0; i < count; i++) {
    if (types[i] == EN) {
      j = i;
      while (j >= 0) {
        if (types[j] == L) {
          types[i] = L;
          break;
        }
        else if (types[j] == R || types[j] == AL) {
          break;
        }
        j--;
      }
    }
  }

 /* Rule (N1)
  * N1. A sequence of neutrals takes the direction of the surrounding
  * strong text if the text on both sides has the same direction. European
  * and Arabic numbers are treated as though they were R.
  */
  if (count >= 2 && types[0] == ON) {
    if ((types[1] == R) || (types[1] == EN) || (types[1] == AN))
      types[0] = R;
    else if (types[1] == L)
      types[0] = L;
  }
  for (i = 1; i < (count - 1); i++) {
    if (types[i] == ON) {
      if (types[i - 1] == L) {
        j = i;
        while (j < (count - 1) && types[j] == ON) {
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
        while (j < (count - 1) && types[j] == ON) {
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
  if (count >= 2 && types[count - 1] == ON) {
    if (types[count - 2] == R || types[count - 2] == EN ||
        types[count - 2] == AN)
      types[count - 1] = R;
    else if (types[count - 2] == L)
      types[count - 1] = L;
  }

 /* Rule (N2)
  * N2. Any remaining neutrals take the embedding direction.
  */
  for (i = 0; i < count; i++) {
    if (types[i] == ON) {
      if ((levels[i] % 2) == 0)
        types[i] = L;
      else
        types[i] = R;
    }
  }

  trace_bidi("<I1");
 /* Rule (I1)
  * I1. For all characters with an even (left-to-right) embedding
  * direction, those of type R go up one level and those of type AN or
  * EN go up two levels.
  */
  for (i = 0; i < count; i++) {
    if ((levels[i] % 2) == 0) {
      if (types[i] == R)
        levels[i] += 1;
      else if (types[i] == AN || types[i] == EN)
        levels[i] += 2;
    }
  }

 /* Rule (I2)
  * I2. For all characters with an odd (right-to-left) embedding direction,
  * those of type L, EN or AN go up one level.
  */
  for (i = 0; i < count; i++) {
    if ((levels[i] % 2) == 1) {
      if (types[i] == L || types[i] == EN || types[i] == AN)
        levels[i] += 1;
    }
  }
  trace_bidi(">I2");

 /* Rule (L1)
  * L1. On each line, reset the embedding level of the following characters
  * to the paragraph embedding level:
  *   (1) segment separators,
  *   (2) paragraph separators,
  *   (3) any sequence of whitespace characters preceding a 
  *       segment separator or paragraph separator,
  *   (4) and any sequence of white space characters at the end of the line.
  * The types of characters used here are the original types, not those
  * modified by the previous phase.
  */
  j = count - 1;
  while (j > 0 && (bidi_class_of(j) == WS)) {
    j--;
  }
  if (j < (count - 1)) {
    for (j++; j < count; j++)
      levels[j] = paragraphLevel;
  }
  for (i = 0; i < count; i++) {
    tempType = bidi_class_of(i);
    if (tempType == WS) {
      j = i;
      while (j < count && (bidi_class_of(j) == WS)) {
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

 /* Rule (L4)
  * L4. A character that possesses the mirrored property as specified by
  * Section 4.7, Mirrored, must be depicted by a mirrored glyph if the
  * resolved directionality of that character is R.
  */
 /* Note: this is implemented before L2 for efficiency */
  for (i = 0; i < count; i++)
    if ((levels[i] % 2) == 1)
      line[i].wc = mirror(line[i].wc);

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
  while (tempType > 0) {        /* loop from highest level to the least odd, */
   /* which i assume is 1 */
    flipThisRun(line, levels, tempType, count);
    tempType--;
  }

 /* Rule (L3) NOT IMPLEMENTED
  * L3. Combining marks applied to a right-to-left base character will at
  * this point precede their base character. If the rendering engine
  * expects them to follow the base characters in the final display
  * process, then the ordering of the marks and the base character must
  * be reversed.
  */
  // This is not relevant for mintty as the combining characters are kept 
  // hidden from this algorithm and are maintained transparently to it.

  return R;
}
