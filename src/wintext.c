// wintext.c (part of mintty)
// Copyright 2008-13 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"
#include "charset.h"  // wcscpy, combiningdouble
#include "config.h"
#include "winimg.h"  // winimg_paint

#include <winnls.h>
#include <usp10.h>  // Uniscribe


enum {
  FONT_NORMAL    = 0x00,
  FONT_BOLD      = 0x01,
  FONT_ITALIC    = 0x02,
  FONT_BOLDITAL  = FONT_BOLD | FONT_ITALIC,
  FONT_UNDERLINE = 0x04,
  FONT_BOLDUND   = FONT_BOLD | FONT_UNDERLINE,
  FONT_STRIKEOUT = 0x08,
  FONT_HIGH      = 0x10,
  FONT_ZOOMFULL  = 0x20,
  FONT_WIDE      = 0x40,
  FONT_NARROW    = 0x80,
  FONT_MAXNO     = FONT_WIDE + FONT_NARROW
};

enum {LDRAW_CHAR_NUM = 31, LDRAW_CHAR_TRIES = 4};

// Possible linedraw character mappings, in order of decreasing suitability.
// The first choice is the same as used by xterm in most cases,
// except the diamond for which the narrower form is more authentic
// (see http://vt100.net/docs/vt220-rm/table2-4.html).
// The last resort for each is an ASCII character, which we assume will be
// available in any font.
static const wchar linedraw_chars[LDRAW_CHAR_NUM][LDRAW_CHAR_TRIES] = {
  {0x2666, 0x25C6, '*'},           // 0x60 '`' Diamond ♦ ◆
  {0x2592, '#'},                   // 0x61 'a' Checkerboard (error)
  {0x2409, 0x2192, 0x01AD, 't'},   // 0x62 'b' Horizontal tab
  {0x240C, 0x21A1, 0x0192, 'f'},   // 0x63 'c' Form feed
  {0x240D, 0x21B5, 0x027C, 'r'},   // 0x64 'd' Carriage return
  {0x240A, 0x21B4, 0x019E, 'n'},   // 0x65 'e' Linefeed
  {0x00B0, 'o'},                   // 0x66 'f' Degree symbol
  {0x00B1, '~'},                   // 0x67 'g' Plus/minus
  {0x2424, 0x21B4, 0x019E, 'n'},   // 0x68 'h' Newline
  {0x240B, 0x2193, 0x028B, 'v'},   // 0x69 'i' Vertical tab
  {0x2518, '+'},                   // 0x6A 'j' Lower-right corner
  {0x2510, '+'},                   // 0x6B 'k' Upper-right corner
  {0x250C, '+'},                   // 0x6C 'l' Upper-left corner
  {0x2514, '+'},                   // 0x6D 'm' Lower-left corner
  {0x253C, '+'},                   // 0x6E 'n' Crossing lines
  {0x23BA, 0x203E, ' '},           // 0x6F 'o' High horizontal line
  {0x23BB, 0x207B, ' '},           // 0x70 'p' Medium-high horizontal line
  {0x2500, 0x2014, '-'},           // 0x71 'q' Middle horizontal line
  {0x23BC, 0x208B, ' '},           // 0x72 'r' Medium-low horizontal line
  {0x23BD, '_'},                   // 0x73 's' Low horizontal line
  {0x251C, '+'},                   // 0x74 't' Left "T"
  {0x2524, '+'},                   // 0x75 'u' Right "T"
  {0x2534, '+'},                   // 0x76 'v' Bottom "T"
  {0x252C, '+'},                   // 0x77 'w' Top "T"
  {0x2502, '|'},                   // 0x78 'x' Vertical bar
  {0x2264, '#'},                   // 0x79 'y' Less than or equal to
  {0x2265, '#'},                   // 0x7A 'z' Greater than or equal to
  {0x03C0, '#'},                   // 0x7B '{' Pi
  {0x2260, '#'},                   // 0x7C '|' Not equal to
  {0x00A3, 'L'},                   // 0x7D '}' UK pound sign
  {0x00B7, '.'},                   // 0x7E '~' Centered dot
};


// colour values
COLORREF colours[COLOUR_NUM];

// diagnostic information flag
bool show_charinfo = false;


// master font family properties
LOGFONT lfont;
// logical font size, as configured (< 0: pixel size)
int font_size;
// scaled font size; pure font height, without spacing
static int font_height;
// character cell size, including spacing:
int cell_width, cell_height;
// border padding:
int PADDING = 1;
// width mode
bool font_ambig_wide;


typedef enum {BOLD_SHADOW, BOLD_FONT} BOLD_MODE;
typedef enum {UND_LINE, UND_FONT} UND_MODE;

struct charpropcache {
  uint width: 2;
  xchar ch: 21;
} __attribute__((packed));

// font family properties
struct fontfam {
  wstring name;
  wstring name_reported;
  int weight;
  bool isbold;
  HFONT fonts[FONT_MAXNO];
  bool fontflag[FONT_MAXNO];
  bool font_dualwidth;
  struct charpropcache * cpcache[FONT_BOLDITAL + 1];
  uint cpcachelen[FONT_BOLDITAL + 1];
  int fw_norm;
  int fw_bold;
  BOLD_MODE bold_mode;
  UND_MODE und_mode;
  int row_spacing, col_spacing;
  int descent;
  // VT100 linedraw character mappings for current font:
  wchar win_linedraw_chars[LDRAW_CHAR_NUM];
} fontfamilies[11];

wchar
win_linedraw_char(int i)
{
  int findex = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  struct fontfam * ff = &fontfamilies[findex];
  return ff->win_linedraw_chars[i];
}


char *
fontpropinfo()
{
  //__ Options - Text: font properties information ("leading" ("ledding"): add. row spacing)
  char * fontinfopat = _("Leading: %d, Bold: %s, Underline: %s");
  //__ Options - Text: font properties: value taken from font
  char * fontinfo_font = _("font");
  //__ Options - Text: font properties: value affected by option
  char * fontinfo_manual = _("manual");
  int taglen = max(strlen(fontinfo_font), strlen(fontinfo_manual));
  char * fontinfo = newn(char, strlen(fontinfopat) + 23 + 2 * taglen);
  sprintf(fontinfo, fontinfopat, fontfamilies->row_spacing, 
          fontfamilies->bold_mode ? fontinfo_font : fontinfo_manual,
          fontfamilies->und_mode ? fontinfo_font : fontinfo_manual);
  return fontinfo;
}


uint
colour_dist(colour a, colour b)
{
  return
    2 * sqr(red(a) - red(b)) +
    4 * sqr(green(a) - green(b)) +
    1 * sqr(blue(a) - blue(b));
}

#define dont_debug_brighten

colour
brighten(colour c, colour against)
{
  uint r = red(c), g = green(c), b = blue(c);
  // "brighten" away from the background:
  // if we are closer to black than the contrast reference, rather darken
  bool darken = colour_dist(c, 0) < colour_dist(against, 0);
#ifdef debug_brighten
  printf("%s %06X against %06X\n", darken ? "darkening" : "brighting", c, against);
#endif

  uint _brighter() {
    uint s = min(85, 255 - max(max(r, g), b));
    return make_colour(r + s, g + s, b + s);
  }
  uint _darker() {
    int sub = 70;
    return make_colour(max(0, (int)r - sub), max(0, (int)g - sub), max(0, (int)b - sub));
  }

  colour bright;
  uint thrsh = 22222;  // contrast threshhold;
                       // if we're closer to either fg or bg,
                       // turn "brightening" into the other direction

  if (darken) {
    bright = _darker();
#ifdef debug_brighten
    printf("darker %06X -> %06X dist %d\n", c, bright, colour_dist(c, bright));
#endif
    if (colour_dist(bright, c) < thrsh || colour_dist(bright, against) < thrsh) {
      bright = _brighter();
#ifdef debug_brighten
      printf("   fix %06X -> %06X dist %d/%d\n", c, bright, colour_dist(bright, c), colour_dist(bright, against));
#endif
    }
  }
  else {
    bright = _brighter();
#ifdef debug_brighten
    printf("lightr %06X -> %06X dist %d\n", c, bright, colour_dist(c, bright));
#endif
    if (colour_dist(bright, c) < thrsh || colour_dist(bright, against) < thrsh) {
      bright = _darker();
#ifdef debug_brighten
      printf("   fix %06X -> %06X dist %d/%d\n", c, bright, colour_dist(bright, c), colour_dist(bright, against));
#endif
    }
  }

  return bright;
}

static uint
get_font_quality(void)
{
  return
    (uchar[]){
      [FS_DEFAULT] = DEFAULT_QUALITY,
      [FS_NONE] = NONANTIALIASED_QUALITY,
      [FS_PARTIAL] = ANTIALIASED_QUALITY,
      [FS_FULL] = CLEARTYPE_QUALITY
    }[(int)cfg.font_smoothing];
}

#define dont_debug_create_font

#define dont_debug_fonts

#ifdef debug_fonts
#define trace_font(params)	printf params
#else
#define trace_font(params)	
#endif

static HFONT
create_font(wstring name, int weight, bool underline)
{
#ifdef debug_create_font
  printf("font [??]: %d (size %d) 0 w%4d i0 u%d s0\n", font_height, font_size, weight, underline);
#endif
  return
    CreateFontW(
      font_height, 0, 0, 0, weight, false, underline, false,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      get_font_quality(), FIXED_PITCH | FF_DONTCARE,
      name
    );
}

static int
row_padding(int i, int e)
{
  // may look nicer; used to break box characters; for background discussion,
  // see https://github.com/mintty/mintty/issues/631#issuecomment-279690468
  static bool allow_add_font_padding = true;

  if (i == 0 && e == 0)
    if (allow_add_font_padding)
      return 2;
    else
      return 0;
  else {
    int exc = 0;
    if (i > 3)
      exc = i - 3;
    int adj = e - exc;
    if (allow_add_font_padding || adj <= 0)
      return adj;
    else
      return 0;
  }
}

static void
show_font_warning(struct fontfam * ff, char * msg)
{
  // suppress multiple font error messages
  if (ff->name_reported && wcscmp(ff->name_reported, ff->name) == 0) {
    return;
  }
  else {
    if (ff->name_reported)
      delete(ff->name_reported);
    ff->name_reported = wcsdup(ff->name);
  }

  char * fn = cs__wcstoutf(ff->name);
  char * fullmsg;
  int len = asprintf(&fullmsg, "%s:\n%s", msg, fn);
  free(fn);
  if (len > 0) {
    show_message(fullmsg, MB_ICONWARNING);
    free(fullmsg);
  }
  else
    show_message(msg, MB_ICONWARNING);
}

#ifndef TCI_SRCLOCALE
//old MinGW
#define TCI_SRCLOCALE 0x1000
#endif

static UINT
get_default_charset(void)
{
  CHARSETINFO csi;

  long int acp = GetACP();
  int ok = TranslateCharsetInfo((DWORD *)acp, &csi, TCI_SRCCODEPAGE);
  if (ok)
    return csi.ciCharset;
  else
    return DEFAULT_CHARSET;
}

static void
adjust_font_weights(struct fontfam * ff)
{
  LOGFONTW lf;
#if CYGWIN_VERSION_API_MINOR >= 201
  swprintf(lf.lfFaceName, lengthof(lf.lfFaceName), W("%ls"), ff->name);
#else
  if (wcslen(ff->name) < lengthof(lf.lfFaceName))
    wcscpy(lf.lfFaceName, ff->name);
  else
    wcscpy(lf.lfFaceName, W(""));
#endif
  lf.lfPitchAndFamily = 0;
  //lf.lfCharSet = ANSI_CHARSET;   // report only ANSI character range
  // use this to avoid double error popup (e.g. Font=David):
  lf.lfCharSet = DEFAULT_CHARSET;  // report all supported char ranges

  // find the closest available widths such that
  // fw_norm_0 <= ff->fw_norm <= fw_norm_1
  // fw_bold_0 <= ff->fw_bold <= fw_bold_1
  int fw_norm_0 = 0;
  int fw_bold_0 = 0;
  int fw_norm_1 = 1000;
  int fw_bold_1 = 1001;
  bool font_found = false;
  bool ansi_found = false;
  int default_charset = get_default_charset();
  bool cs_found = default_charset == DEFAULT_CHARSET;

  int CALLBACK enum_fonts(const LOGFONTW * lfp, const TEXTMETRICW * tmp, DWORD fontType, LPARAM lParam)
  {
    (void)tmp;
    (void)fontType;
    (void)lParam;

    //trace_font(("%ls %ldx%ld (%ldx%ld) %ld it %d cs %d %s\n", lfp->lfFaceName, (long int)lfp->lfWidth, (long int)lfp->lfHeight, (long int)tmp->tmAveCharWidth, (long int)tmp->tmHeight, (long int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));
    trace_font(("%ls %ldx%ld %ld it %d cs %d %s\n", lfp->lfFaceName, (long int)lfp->lfWidth, (long int)lfp->lfHeight, (long int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));

    font_found = true;
    if (lfp->lfCharSet == ANSI_CHARSET)
      ansi_found = true;
    if (lfp->lfCharSet == default_charset || lfp->lfCharSet == DEFAULT_CHARSET)
      cs_found = true;

    if (lfp->lfWeight > fw_norm_0 && lfp->lfWeight <= ff->fw_norm)
      fw_norm_0 = lfp->lfWeight;
    if (lfp->lfWeight > fw_bold_0 && lfp->lfWeight <= ff->fw_bold)
      fw_bold_0 = lfp->lfWeight;
    if (lfp->lfWeight < fw_norm_1 && lfp->lfWeight >= ff->fw_norm)
      fw_norm_1 = lfp->lfWeight;
    if (lfp->lfWeight < fw_bold_1 && lfp->lfWeight >= ff->fw_bold)
      fw_bold_1 = lfp->lfWeight;

    return 1;  // continue
  }

  HDC dc = GetDC(0);
  EnumFontFamiliesExW(dc, &lf, enum_fonts, 0, 0);
  trace_font(("font width (%d)%d(%d)/(%d)%d(%d)", fw_norm_0, ff->fw_norm, fw_norm_1, fw_bold_0, ff->fw_bold, fw_bold_1));
  ReleaseDC(0, dc);

  // check if no font found
  if (!font_found) {
    show_font_warning(ff, _("Font not found, using system substitute"));
    ff->fw_norm = 400;
    ff->fw_bold = 700;
    trace_font(("//\n"));
    return;
  }
  if (!ansi_found && !cs_found) {
    show_font_warning(ff, _("Font has limited support for character ranges"));
  }

  // find available widths closest to selected widths
  if (abs(ff->fw_norm - fw_norm_0) <= abs(ff->fw_norm - fw_norm_1) && fw_norm_0 > 0)
    ff->fw_norm = fw_norm_0;
  else if (fw_norm_1 < 1000)
    ff->fw_norm = fw_norm_1;
  if (abs(ff->fw_bold - fw_bold_0) < abs(ff->fw_bold - fw_bold_1) || fw_bold_1 > 1000)
    ff->fw_bold = fw_bold_0;
  else if (fw_bold_1 < 1001)
    ff->fw_bold = fw_bold_1;
  // ensure bold is bolder than normal
  if (ff->fw_bold <= ff->fw_norm) {
    trace_font((" -> %d/%d", ff->fw_norm, ff->fw_bold));
    if (fw_norm_0 < ff->fw_norm && fw_norm_0 > 0)
      ff->fw_norm = fw_norm_0;
    if (ff->fw_bold - ff->fw_norm < 300) {
      if (fw_bold_1 > ff->fw_bold && fw_bold_1 < 1001)
        ff->fw_bold = fw_bold_1;
      else
        ff->fw_bold = min(ff->fw_norm + 300, 1000);
    }
  }
  // enforce preselected boldness
  int selweight = ff->weight;
  if (selweight < 700 && ff->isbold)
    selweight = 700;
  if (selweight - ff->fw_norm >= 300) {
    trace_font((" -> %d(%d)/%d", ff->fw_norm, selweight, ff->fw_bold));
    ff->fw_norm = selweight;
    ff->fw_bold = min(ff->fw_norm + 300, 1000);
  }
  trace_font((" -> %d/%d\n", ff->fw_norm, ff->fw_bold));
}

/*
 * Initialise all the fonts of a font family we will need initially:
   Normal (the ordinary font), and optionally bold and underline;
   Other font variations are done if/when they are needed (another_font).

   We also:
   - check the font width and height, correcting our guesses if necessary.
   - verify that the bold font is the same width as the ordinary one, 
     and engage shadow bolding if not.
   - verify that the underlined font is the same width as the ordinary one, 
     and engage manual underlining if not.
 */
static void
win_init_fontfamily(HDC dc, int findex)
{
  struct fontfam * ff = &fontfamilies[findex];

  trace_resize(("--- init_fontfamily\n"));
  TEXTMETRIC tm;
  int fontsize[FONT_UNDERLINE + 1];

  for (uint i = 0; i < FONT_BOLDITAL; i++) {
    if (ff->fonts[i])
      delete(ff->cpcache[i]);
    ff->cpcache[i] = 0;
    ff->cpcachelen[i] = 0;
  }
  for (uint i = 0; i < FONT_MAXNO; i++) {
    if (ff->fonts[i]) {
      DeleteObject(ff->fonts[i]);
      ff->fonts[i] = 0;
    }
    ff->fontflag[i] = 0;
  }

  // if initialized as BOLD_SHADOW then real bold is never attempted
  ff->bold_mode = BOLD_FONT;

  ff->und_mode = UND_FONT;
  if (cfg.underl_manual || cfg.underl_colour != (colour)-1)
    ff->und_mode = UND_LINE;

  if (ff->weight) {
    ff->fw_norm = ff->weight;
    ff->fw_bold = min(ff->fw_norm + 300, 1000);
    // adjust selected font weights to available font weights
    trace_font(("-> Weight %d/%d\n", ff->fw_norm, ff->fw_bold));
    adjust_font_weights(ff);
    trace_font(("->     -> %d/%d\n", ff->fw_norm, ff->fw_bold));
  }
  else if (ff->isbold) {
    ff->fw_norm = FW_BOLD;
    ff->fw_bold = FW_HEAVY;
    trace_font(("-> IsBold %d/%d\n", ff->fw_norm, ff->fw_bold));
  }
  else {
    ff->fw_norm = FW_DONTCARE;
    ff->fw_bold = FW_BOLD;
    trace_font(("-> normal %d/%d\n", ff->fw_norm, ff->fw_bold));
  }

  ff->fonts[FONT_NORMAL] = create_font(ff->name, ff->fw_norm, false);

  LOGFONT logfont;
  GetObject(ff->fonts[FONT_NORMAL], sizeof(LOGFONT), &logfont);
  trace_font(("created font %s %ld it %d cs %d\n", logfont.lfFaceName, (long int)logfont.lfWeight, logfont.lfItalic, logfont.lfCharSet));
  SelectObject(dc, ff->fonts[FONT_NORMAL]);
  GetTextMetrics(dc, &tm);
  if (!tm.tmHeight) {
    // corrupt font installation (e.g. deleted font file)
    show_font_warning(ff, _("Font installation corrupt, using system substitute"));
    wstrset(&ff->name, W(""));
    ff->fonts[FONT_NORMAL] = create_font(ff->name, ff->fw_norm, false);
    GetObject(ff->fonts[FONT_NORMAL], sizeof(LOGFONT), &logfont);
    SelectObject(dc, ff->fonts[FONT_NORMAL]);
    GetTextMetrics(dc, &tm);
  }
  if (!findex)
    lfont = logfont;

#ifdef check_charset_only_for_returned_font
  int default_charset = get_default_charset();
  if (tm.tmCharSet != default_charset && default_charset != DEFAULT_CHARSET) {
    show_font_warning(ff, _("Font does not support system locale"));
  }
#endif

  if (!findex) {
    ff->row_spacing = row_padding(tm.tmInternalLeading, tm.tmExternalLeading);
    trace_font(("h %ld asc %ld dsc %ld ild %ld eld %ld %ls\n", (long int)tm.tmHeight, (long int)tm.tmAscent, (long int)tm.tmDescent, (long int)tm.tmInternalLeading, (long int)tm.tmExternalLeading, ff->name));
    ff->row_spacing += cfg.row_spacing;
    if (ff->row_spacing < -tm.tmDescent)
      ff->row_spacing = -tm.tmDescent;
    trace_font(("row spacing int %ld ext %ld -> %+d; add %+d -> %+d; desc %ld -> %+d %ls\n", 
        (long int)tm.tmInternalLeading, (long int)tm.tmExternalLeading, row_padding(tm.tmInternalLeading, tm.tmExternalLeading),
        cfg.row_spacing, row_padding(tm.tmInternalLeading, tm.tmExternalLeading) + cfg.row_spacing,
        (long int)tm.tmDescent, ff->row_spacing, ff->name));
    ff->col_spacing = cfg.col_spacing;

    cell_height = tm.tmHeight + ff->row_spacing;
    cell_width = tm.tmAveCharWidth + ff->col_spacing;

    PADDING = tm.tmAveCharWidth;
    if (cfg.padding >= 0 && cfg.padding < PADDING)
      PADDING = cfg.padding;
  }
  else {
    ff->row_spacing = cell_height - tm.tmHeight;
    ff->col_spacing = cell_width - tm.tmAveCharWidth;
  }

#ifdef debug_create_font
  printf("size %d -> height %d -> height %d\n", font_size, font_height, cell_height);
#endif

  ff->font_dualwidth = (tm.tmMaxCharWidth >= tm.tmAveCharWidth * 3 / 2);

  // Determine whether ambiguous-width characters are wide in this font */
  float latin_char_width, greek_char_width, line_char_width;
  GetCharWidthFloatW(dc, 0x0041, 0x0041, &latin_char_width);
  GetCharWidthFloatW(dc, 0x03B1, 0x03B1, &greek_char_width);
  GetCharWidthFloatW(dc, 0x2500, 0x2500, &line_char_width);
  if (!findex)
    font_ambig_wide =
      greek_char_width >= latin_char_width * 1.5 ||
      line_char_width  >= latin_char_width * 1.5;

#ifdef debug_win_char_width
  int w_latin = win_char_width(0x0041);
  int w_greek = win_char_width(0x03B1);
  int w_lines = win_char_width(0x2500);
  printf("%04X %5.2f %d\n", 0x0041, latin_char_width, w_latin);
  printf("%04X %5.2f %d\n", 0x03B1, greek_char_width, w_greek);
  printf("%04X %5.2f %d\n", 0x2500, line_char_width, w_lines);
  bool faw = w_greek > w_latin || w_lines > w_latin;
  printf("font faw %d (dual %d [ambig %d])\n", faw, ff->font_dualwidth, font_ambig_wide);
#endif

  // Initialise VT100 linedraw character mappings.
  // See what glyphs are available.
  ushort glyphs[LDRAW_CHAR_NUM][LDRAW_CHAR_TRIES];
  GetGlyphIndicesW(dc, *linedraw_chars, LDRAW_CHAR_NUM * LDRAW_CHAR_TRIES,
                   *glyphs, true);

  // For each character, try the list of possible mappings until either we
  // find one that has a glyph in the font or we hit the ASCII fallback.
  for (uint i = 0; i < LDRAW_CHAR_NUM; i++) {
    uint j = 0;
    while (linedraw_chars[i][j] >= 0x80 &&
           (glyphs[i][j] == 0xFFFF || glyphs[i][j] == 0x1F))
      j++;
    ff->win_linedraw_chars[i] = linedraw_chars[i][j];
#define draw_vt100_line_drawing_chars
#ifdef draw_vt100_line_drawing_chars
    if ('j' - '`' <= i && i <= 'x' - '`')
      ff->win_linedraw_chars[i] = linedraw_chars[i][0];
#endif
  }

  ff->fonts[FONT_UNDERLINE] = create_font(ff->name, ff->fw_norm, true);

 /*
  * Some fonts, e.g. 9-pt Courier, draw their underlines
  * outside their character cell. We successfully prevent
  * screen corruption by clipping the text output, but then
  * we lose the underline completely. Here we try to work
  * out whether this is such a font, and if it is, we set a
  * flag that causes underlines to be drawn by hand.
  *
  * Having tried other more sophisticated approaches (such
  * as examining the TEXTMETRIC structure or requesting the
  * height of a string), I think we'll do this the brute
  * force way: we create a small bitmap, draw an underlined
  * space on it, and test to see whether any pixels are
  * foreground-coloured. (Since we expect the underline to
  * go all the way across the character cell, we only search
  * down a single column of the bitmap, half way across.)
  */
  if (ff->und_mode == UND_FONT) {
    HDC und_dc = CreateCompatibleDC(dc);
    HBITMAP und_bm = CreateCompatibleBitmap(dc, cell_width, cell_height);
    HBITMAP und_oldbm = SelectObject(und_dc, und_bm);
    SelectObject(und_dc, ff->fonts[FONT_UNDERLINE]);
    SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    SetTextColor(und_dc, RGB(255, 255, 255));
    SetBkColor(und_dc, RGB(0, 0, 0));
    SetBkMode(und_dc, OPAQUE);
    ExtTextOutA(und_dc, 0, 0, ETO_OPAQUE, null, " ", 1, null);

    bool gotit = false;
    // look for font-generated underline in character cell
    //int i = 0;
    // look for font-generated underline in descender section only
    int i = tm.tmAscent;
    //int i = tm.tmAscent + 1;
    for (; i < cell_height; i++) {
      COLORREF c = GetPixel(und_dc, cell_width / 2, i);
      if (c != RGB(0, 0, 0))
        gotit = true;
    }
    SelectObject(und_dc, und_oldbm);
    DeleteObject(und_bm);
    DeleteDC(und_dc);
    if (!gotit) {
      trace_font(("ul outbox %ls\n", ff->name));
      ff->und_mode = UND_LINE;
      DeleteObject(ff->fonts[FONT_UNDERLINE]);
      ff->fonts[FONT_UNDERLINE] = 0;
    }
  }

  if (ff->bold_mode == BOLD_FONT)
    ff->fonts[FONT_BOLD] = create_font(ff->name, ff->fw_bold, false);

  ff->descent = tm.tmAscent + 1;
  if (ff->descent >= cell_height)
    ff->descent = cell_height - 1;

  for (uint i = 0; i < lengthof(fontsize); i++) { // could skip FONT_ITALIC here
    if (ff->fonts[i]) {
      if (SelectObject(dc, ff->fonts[i]) && GetTextMetrics(dc, &tm))
        fontsize[i] = tm.tmAveCharWidth + 256 * tm.tmHeight;
      else
        fontsize[i] = -i;
    }
    else
      fontsize[i] = -i;
  }

  if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
    trace_font(("ul size!= %ls\n", ff->name));
    ff->und_mode = UND_LINE;
    DeleteObject(ff->fonts[FONT_UNDERLINE]);
    ff->fonts[FONT_UNDERLINE] = 0;
  }

  if (ff->bold_mode == BOLD_FONT && fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
    trace_font(("bold_mode %d\n", ff->bold_mode));
    ff->bold_mode = BOLD_SHADOW;
    DeleteObject(ff->fonts[FONT_BOLD]);
    ff->fonts[FONT_BOLD] = 0;
  }
  trace_font(("bold_mode %d\n", ff->bold_mode));
  ff->fontflag[0] = ff->fontflag[1] = ff->fontflag[2] = 1;
}

static wstring
wcscasestr(wstring in, wstring find)
{
#if CYGWIN_VERSION_API_MINOR < 206
#define wcsncasecmp wcsncmp
#endif
  int l = wcslen(find);
  wstring look = in;
  for (int i = 0; i <= (int)wcslen(in) - l; i++, look++) { // uint fails!
    if (0 == wcsncasecmp(look, find, l)) {
      return look;
    }
  }
  return 0;
}

static void
findFraktur(wstring * fnp)
{
  LOGFONTW lf;
  wcscpy(lf.lfFaceName, W(""));
  lf.lfPitchAndFamily = 0;
  lf.lfCharSet = ANSI_CHARSET;   // report only ANSI character range

  int CALLBACK enum_fonts(const LOGFONTW * lfp, const TEXTMETRICW * tmp, DWORD fontType, LPARAM lParam)
  {
    (void)tmp;
    (void)fontType;
    (void)lParam;

    //trace_font(("%ls %ldx%ld (%ldx%ld) %ld it %d cs %d %s\n", lfp->lfFaceName, (long int)lfp->lfWidth, (long int)lfp->lfHeight, (long int)tmp->tmAveCharWidth, (long int)tmp->tmHeight, (long int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));
    trace_font(("%ls %ldx%ld %ld it %d cs %d %s\n", lfp->lfFaceName, (long int)lfp->lfWidth, (long int)lfp->lfHeight, (long int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));
    if ((lfp->lfPitchAndFamily & 3) == FIXED_PITCH
     && !lfp->lfCharSet
     && lfp->lfFaceName[0] != '@'
       )
    {
      if (wcscasestr(lfp->lfFaceName, W("Fraktur"))) {
        *fnp = wcsdup(lfp->lfFaceName);
        return 0;  // done
      }
      else if (wcscasestr(lfp->lfFaceName, W("Blackletter"))) {
        *fnp = wcsdup(lfp->lfFaceName);
        // continue to look for "Fraktur"
      }
    }
    return 1;  // continue
  }

  HDC dc = GetDC(0);
  EnumFontFamiliesExW(dc, 0, enum_fonts, 0, 0);
  trace_font(("font width (%d)%d(%d)/(%d)%d(%d)", fw_norm_0, ff->fw_norm, fw_norm_1, fw_bold_0, ff->fw_bold, fw_bold_1));
  ReleaseDC(0, dc);
}

/*
 * Initialize fonts for all configured font families.
 */
void
win_init_fonts(int size)
{
  trace_resize(("--- init_fonts %d\n", size));

  HDC dc = GetDC(wnd);

  font_size = size;
#ifdef debug_dpi
  printf("dpi %d dev %d\n", dpi, GetDeviceCaps(dc, LOGPIXELSY));
#endif
  if (cfg.handle_dpichanged && per_monitor_dpi_aware)
    font_height =
      font_size > 0 ? -MulDiv(font_size, dpi, 72) : -font_size;
      // dpi is determined initially and via WM_WINDOWPOSCHANGED;
      // if WM_DPICHANGED were used, this would need to be modified
  else
    font_height =
      font_size > 0 ? -MulDiv(font_size, GetDeviceCaps(dc, LOGPIXELSY), 72) : -font_size;

  static bool initinit = true;
  for (uint fi = 0; fi < lengthof(fontfamilies); fi++) {
    if (!fi) {
      fontfamilies[fi].name = cfg.font.name;
      fontfamilies[fi].weight = cfg.font.weight;
      fontfamilies[fi].isbold = cfg.font.isbold;
    }
    else {
      fontfamilies[fi].name = cfg.fontfams[fi].name;
      fontfamilies[fi].weight = cfg.fontfams[fi].weight;
      fontfamilies[fi].isbold = false;
    }
    if (fi == 20 - 10 && !*(fontfamilies[fi].name))
      findFraktur(&fontfamilies[fi].name);
    if (initinit)
      fontfamilies[fi].name_reported = null;

    win_init_fontfamily(dc, fi);
  }
  initinit = false;

  ReleaseDC(wnd, dc);
}

wstring
win_get_font(uint fi)
{
  if (fi < lengthof(fontfamilies))
    return fontfamilies[fi].name;
  else
    return null;
}

void
win_change_font(uint fi, wstring fn)
{
  if (fi < lengthof(fontfamilies)) {
    fontfamilies[fi].name = fn;
    fontfamilies[fi].name_reported = null;
    HDC dc = GetDC(wnd);
    win_init_fontfamily(dc, fi);
    ReleaseDC(wnd, dc);
    win_adapt_term_size(true, false);
    win_font_cs_reconfig(true);
  }
}

uint
win_get_font_size(void)
{
  return abs(font_size);
}

void
win_set_font_size(int size, bool sync_size_with_font)
{
  trace_resize(("--- win_set_font_size %d %d×%d\n", size, term.rows, term.cols));
  size = size ? sgn(font_size) * min(size, 72) : cfg.font.size;
  if (size != font_size) {
    win_init_fonts(size);
    trace_resize((" (win_set_font_size -> win_adapt_term_size)\n"));
    win_adapt_term_size(sync_size_with_font, false);
  }
}

void
win_zoom_font(int zoom, bool sync_size_with_font)
{
  trace_resize(("--- win_zoom_font %d\n", zoom));
  win_set_font_size(zoom ? max(1, abs(font_size) + zoom) : 0, sync_size_with_font);
}

static HDC dc;
static enum { UPDATE_IDLE, UPDATE_BLOCKED, UPDATE_PENDING } update_state;
static bool ime_open;

void
win_paint(void)
{
  PAINTSTRUCT p;
  dc = BeginPaint(wnd, &p);

  term_invalidate(
    (p.rcPaint.left - PADDING) / cell_width,
    (p.rcPaint.top - PADDING) / cell_height,
    (p.rcPaint.right - PADDING - 1) / cell_width,
    (p.rcPaint.bottom - PADDING - 1) / cell_height
  );

  if (update_state != UPDATE_PENDING) {
    term_paint();
    winimg_paint();
  }

  if (p.fErase || p.rcPaint.left < PADDING ||
      p.rcPaint.top < PADDING ||
      p.rcPaint.right >= PADDING + cell_width * term.cols ||
      p.rcPaint.bottom >= PADDING + cell_height * term.rows) {
    colour bg_colour = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
    HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(bg_colour));
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, bg_colour));

    IntersectClipRect(dc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
                      p.rcPaint.bottom);

    ExcludeClipRect(dc, PADDING, PADDING,
                    PADDING + cell_width * term.cols,
                    PADDING + cell_height * term.rows);

    Rectangle(dc, p.rcPaint.left, p.rcPaint.top,
                  p.rcPaint.right, p.rcPaint.bottom);

    DeleteObject(SelectObject(dc, oldbrush));
    DeleteObject(SelectObject(dc, oldpen));
  }

  EndPaint(wnd, &p);
}

#define dont_debug_cursor 1

static struct charnameentry {
  xchar uc;
  string un;
} * charnametable = null;
static int charnametable_len = 0;
static int charnametable_alloced = 0;

static void
init_charnametable()
{
  if (charnametable)
    return;

  void add_charname(uint cc, char * cn) {
    if (charnametable_len >= charnametable_alloced) {
      charnametable_alloced += 999;
      if (!charnametable)
        charnametable = newn(struct charnameentry, charnametable_alloced);
      else
        charnametable = renewn(charnametable, charnametable_alloced);
    }

    charnametable[charnametable_len].uc = cc;
    charnametable[charnametable_len].un = strdup(cn);
    charnametable_len++;
  }

  char * cnfn = get_resource_file(W("info"), W("charnames.txt"), false);
  FILE * cnf = fopen(cnfn, "r");
  if (cnf) {
    uint cc;
    char cn[100];
    while (fscanf(cnf, "%X %[- A-Z0-9]", &cc, cn) == 2) {
      add_charname(cc, cn);
    }
    fclose(cnf);
  }
  else {
    cnf = fopen("/usr/share/unicode/ucd/UnicodeData.txt", "r");
    if (!cnf)
      return;
    FILE * crf = fopen("/usr/share/unicode/ucd/NameAliases.txt", "r");
    uint ccorr = 0;
    char buf[100];
    char nbuf[100];
    while (fgets(buf, sizeof(buf), cnf)) {
      uint cc;
      char cn[99];
      if (sscanf(buf, "%X;%[- A-Z0-9];", &cc, cn) == 2) {
        //0020;SPACE;Zs;0;WS;;;;;N;;;;;
        if (crf) {
          while (ccorr < cc && fgets(nbuf, sizeof(nbuf), crf)) {
            sscanf(nbuf, "%X;", &ccorr);
          }
          if (ccorr == cc && strstr(nbuf, ";correction")) {
            //2118;WEIERSTRASS ELLIPTIC FUNCTION;correction
            sscanf(nbuf, "%X;%[- A-Z0-9];", &ccorr, cn);
          }
        }
        add_charname(cc, cn);
      }
    }
    fclose(cnf);
    if (crf)
      fclose(crf);
  }
}

static char *
charname(xchar ucs)
{
  // binary search in table
  int min = 0;
  int max = charnametable_len - 1;
  int mid;
  while (max >= min) {
    unsigned long midu;
    unsigned char * mide;
    mid = (min + max) / 2;
    mide = (unsigned char *) charnametable[mid].un;
    midu = charnametable[mid].uc;
    if (midu < ucs) {
      min = mid + 1;
    } else if (midu > ucs) {
      max = mid - 1;
    } else {
      return (char *) mide;
    }
  }
  return "";
}

void
toggle_charinfo()
{
  show_charinfo = !show_charinfo;
}

static void
show_curchar_info(char tag)
{
  if (!show_charinfo)
    return;
  init_charnametable();
  (void)tag;
  static termchar * pp = 0;
  static termchar prev; // = (termchar) {.cc_next = 0, .chr = 0, .attr = CATTR_DEFAULT};

  void show_char_msg(char * cs) {
    static char * prev = null;
    if (!prev || 0 != strcmp(cs, prev)) {
      //printf("[%c]%s\n", tag, cs);
      SetWindowTextA(wnd, cs);
    }
    if (prev)
      free(prev);
    prev = cs;
  }

  void show_char_info(termchar * cpoi) {
    // return if base character same as previous and no combining chars
    if (cpoi == pp && cpoi && cpoi->chr == prev.chr && !cpoi->cc_next)
      return;

    char * cs = strdup("");

    pp = cpoi;
    if (cpoi) {
      prev = *cpoi;

      char * cn = strdup("");

      xchar chbase = 0;
#ifdef show_only_1_charname
      bool combined = false;
#endif
      // show char codes
      while (cpoi) {
        cs = renewn(cs, strlen(cs) + 8 + 1);
        char * cp = &cs[strlen(cs)];
        xchar ci;
        if (is_high_surrogate(cpoi->chr) && cpoi->cc_next && is_low_surrogate((cpoi + cpoi->cc_next)->chr)) {
          ci = combine_surrogates(cpoi->chr, (cpoi + cpoi->cc_next)->chr);
          sprintf(cp, "U+%05X ", ci);
          cpoi += cpoi->cc_next;
        }
        else {
          ci = cpoi->chr;
          sprintf(cp, "U+%04X ", ci);
        }
        if (!chbase)
          chbase = ci;
        char * cni = charname(ci);
        if (cni && *cni) {
          cn = renewn(cn, strlen(cn) + strlen(cni) + 4);
          sprintf(&cn[strlen(cn)], "| %s ", cni);
        }

        if (cpoi->cc_next) {
#ifdef show_only_1_charname
          combined = true;
#endif
          cpoi += cpoi->cc_next;
        }
        else
          cpoi = null;
      }
#ifdef show_only_1_charname
      char * cn = charname(chbase);
      char * extra = combined ? " combined..." : "";
      cs = renewn(cs, strlen(cs) + strlen(cn) + strlen(extra) + 1);
      sprintf(&cs[strlen(cs)], "%s%s", cn, extra);
#else
      cs = renewn(cs, strlen(cs) + strlen(cn) + 1);
      sprintf(&cs[strlen(cs)], "%s", cn);
      free(cn);
#endif
    }

    show_char_msg(cs);  // does free(cs);
  }

  int line = term.curs.y - term.disptop;
  if (line < 0 || line >= term.rows) {
    show_char_info(null);
  }
  else {
    termline * displine = term.displines[line];
    termchar * dispchar = &displine->chars[term.curs.x];
    show_char_info(dispchar);
  }
}


void
do_update(void)
{
#if defined(debug_cursor) && debug_cursor > 1
  printf("do_update cursor_on %d @%d,%d\n", term.cursor_on, term.curs.y, term.curs.x);
#endif
  show_curchar_info('u');
  if (update_state == UPDATE_BLOCKED) {
    update_state = UPDATE_IDLE;
    return;
  }

  update_state = UPDATE_BLOCKED;

  dc = GetDC(wnd);

  win_paint_exclude_search(dc);
  term_update_search();

  term_paint();
  winimg_paint();

  ReleaseDC(wnd, dc);

  // Update scrollbar
  if (cfg.scrollbar && term.show_scrollbar) {
    int lines = sblines();
    SCROLLINFO si = {
      .cbSize = sizeof si,
      .fMask = SIF_ALL | SIF_DISABLENOSCROLL,
      .nMin = 0,
      .nMax = lines + term.rows - 1,
      .nPage = term.rows,
      .nPos = lines + term.disptop
    };
    SetScrollInfo(wnd, SB_VERT, &si, true);
  }

  // Update the positions of the system caret and the IME window.
  // (We maintain a caret, even though it's invisible, for the benefit of
  // blind people: apparently some helper software tracks the system caret,
  // so we should arrange to have one.)
  if (term.has_focus) {
    int x = term.curs.x * cell_width + PADDING;
    int y = (term.curs.y - term.disptop) * cell_height + PADDING;
    SetCaretPos(x, y);
    if (ime_open) {
      COMPOSITIONFORM cf = {.dwStyle = CFS_POINT, .ptCurrentPos = {x, y}};
      ImmSetCompositionWindow(imc, &cf);
    }
  }

  // Schedule next update.
  win_set_timer(do_update, 16);
}

void
win_update(void)
{
  trace_resize(("----- win_update\n"));
  if (update_state == UPDATE_IDLE)
    do_update();
  else
    update_state = UPDATE_PENDING;
}

void
win_schedule_update(void)
{
  if (update_state == UPDATE_IDLE)
    win_set_timer(do_update, 16);
  update_state = UPDATE_PENDING;
}


static void
another_font(struct fontfam * ff, int fontno)
{
  int basefont;
  int u, w, i, s, x;

  if (fontno < 0 || fontno >= FONT_MAXNO || ff->fontflag[fontno])
    return;

  basefont = (fontno & ~(FONT_BOLDUND));
  if (basefont != fontno && !ff->fontflag[basefont])
    another_font(ff, basefont);

  w = ff->fw_norm;
  i = false;
  s = false;
  u = false;
  x = cell_width;

  if (fontno & FONT_WIDE)
    x *= 2;
  if (fontno & FONT_NARROW)
    x = (x + 1) / 2;
  if (fontno & FONT_BOLD)
    w = ff->fw_bold;
  if (fontno & FONT_ITALIC)
    i = true;
  if (fontno & FONT_STRIKEOUT)
    s = true;
  if (fontno & FONT_UNDERLINE)
    u = true;
  int y = font_height * (1 + !!(fontno & FONT_HIGH));
  if (fontno & FONT_ZOOMFULL) {
    y = cell_height * (1 + !!(fontno & FONT_HIGH));
    x = cell_width * (1 + !!(fontno & FONT_WIDE));
  }

#ifdef debug_create_font
  printf("font [%02X]: %d (size %d%s%s%s%s) %d w%4d i%d u%d s%d\n", 
	fontno, font_height * (1 + !!(fontno & FONT_HIGH)), font_size, 
	fontno & FONT_HIGH     ? " hi" : "",
	fontno & FONT_WIDE     ? " wd" : "",
	fontno & FONT_NARROW   ? " nr" : "",
	fontno & FONT_ZOOMFULL ? " zf" : "",
	x, w, i, u, s);
#endif
  ff->fonts[fontno] =
    CreateFontW(y, x, 0, 0, w, i, u, s,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                get_font_quality(), FIXED_PITCH | FF_DONTCARE, ff->name);

  ff->fontflag[fontno] = 1;
}

void
win_set_ime_open(bool open)
{
  if (open != ime_open) {
    ime_open = open;
    term.cursor_invalid = true;
    win_update();
  }
}


#define dont_debug_win_text

static void
_trace_line(char * tag, wchar * text, int len)
{
  bool show = false;
  for (int i = 0; i < len; i++)
    if (text[i] != ' ') show = true;
  if (show) {
    printf("%s", tag);
    for (int i = 0; i < len; i++) printf(" %04X", text[i]);
    printf("\n");
  }
}

inline static void
trace_line(char * tag, wchar * text, int len)
{
  _trace_line(tag, text, len);
}

#ifndef debug_win_text
#define trace_line(tag, text, len)	
#endif

static wchar
combsubst(wchar comb)
{
  static const struct {
    wchar comb;
    wchar subst;
    short pref;  // -1: suppress, +1: enforce
  } lookup[] = {
    {0x0300, 0x0060, 0},
    {0x0301, 0x00B4, 0},
    {0x0302, 0x02C6, 0},
    {0x0303, 0x02DC, 0},
    {0x0304, 0x00AF, 0},
    {0x0305, 0x203E, 0},
    {0x0306, 0x02D8, 0},
    {0x0307, 0x02D9, 0},
    {0x0308, 0x00A8, 0},
    {0x030A, 0x02DA, 0},
    {0x030B, 0x02DD, 0},
    {0x030C, 0x02C7, 0},
    {0x0327, 0x00B8, 0},
    {0x0328, 0x02DB, 0},
    {0x0332, 0x005F, 0},
    {0x0333, 0x2017, 0},
    {0x033E, 0x2E2F, -1},	// display broken if substituted
    {0x0342, 0x1FC0, +1},	// display broken if not substituted
    {0x0343, 0x1FBD, 0},
    {0x0344, 0x0385, +1},	// display broken if not substituted
    {0x0345, 0x037A, +1},	// display broken if not substituted
    {0x3099, 0x309B, 0},
    {0x309A, 0x309C, 0},
    {0xA67C, 0xA67E, 0},
    {0xA67D, 0xA67F, 0},
  };

  int i, j, k;

  i = -1;
  j = lengthof(lookup);

  while (j - i > 1) {
    k = (i + j) / 2;
    if (comb < lookup[k].comb)
      j = k;
    else if (comb > lookup[k].comb)
      i = k;
    else {
      // apply heuristic tweaking of the substitution:
      if (lookup[k].pref == 1)
        return lookup[k].subst;
      else if (lookup[k].pref == -1)
        return comb;

      wchar chk = comb;
      win_check_glyphs(&chk, 1);
      if (chk)
        return lookup[k].subst;
      else
        return comb;
    }
  }
  return comb;
}

int
termattrs_equal_fg(cattr * a, cattr * b)
{
  if (a->truefg != b->truefg)
    return false;
#define ATTR_COLOUR_MASK (ATTR_FGMASK | ATTR_BOLD | ATTR_DIM)
  if ((a->attr & ATTR_COLOUR_MASK) != (b->attr & ATTR_COLOUR_MASK))
    return false;
  return true;
}


static int
char1ulen(wchar * text)
{
  if ((text[0] & 0xFC00) == 0xD800 && (text[1] & 0xFC00) == 0xDC00)
    return 2;
  else
    return 1;
}

static SCRIPT_STRING_ANALYSIS ssa;
static bool use_uniscribe;

void
text_out_start(HDC hdc, LPCWSTR psz, int cch, int *dxs)
{
  if (cch == 0)
    use_uniscribe = false;
  if (!use_uniscribe)
    return;

  HRESULT hr = ScriptStringAnalyse(hdc, psz, cch, 0, -1, 
    // could | SSA_FIT and use `width` (from win_text) instead of MAXLONG
    // to justify to monospace cell widths;
    // SSA_LINK is needed for Hangul and default-size CJK
    SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, MAXLONG, 
    NULL, NULL, dxs, NULL, NULL, &ssa);
  if (!SUCCEEDED(hr) && hr != USP_E_SCRIPT_NOT_IN_FONT)
    use_uniscribe = false;
}

void
text_out(HDC hdc, int x, int y, UINT fuOptions, RECT *prc, LPCWSTR psz, int cch, int *dxs)
{
  if (cch == 0)
    return;
#ifdef debug_text_out
  if (*psz >= 0x80) {
    printf("@%3d (%3d):", x, y);
    for (int i = 0; i < cch; i++)
      printf(" %04X", psz[i]);
    printf("\n");
  }
#endif

  if (use_uniscribe)
    ScriptStringOut(ssa, x, y, fuOptions, prc, 0, 0, FALSE);
  else
    ExtTextOutW(hdc, x, y, fuOptions, prc, psz, cch, dxs);
}

void
text_out_end()
{
  if (use_uniscribe)
    ScriptStringFree(&ssa);
}


// applies bold as colour if required, returns true if still needs thickening
static bool
apply_bold_colour(colour_i *pfgi)
{
  // We use two bits to control colouring and thickening for three classes of
  // colours: ANSI (0-7), default, and other (8-256, true). We also reserve one
  // combination for xterm's default (thicken everything, ANSI is also coloured).
  // - "other" is always thickened and never gets colouring.
  // - bold_as_font:  thicken ANSI/default colours.
  // - bold_as_colour: colour ANSI/default colours.
  // Exception if both false: thicken ANSI/default, colour ANSI (xterm's default).
  bool ansi = CCL_ANSI8(*pfgi);
  if (!ansi && !CCL_DEFAULT(*pfgi))
    return true;  // neither ANSI nor default -> always thicken, no colouring

  if (!cfg.bold_as_colour && !cfg.bold_as_font) {  // the exception: xterm-like
    if (ansi)  // coloured
      *pfgi |= 8;  // (BLACK|...|WHITE)_I -> BOLD_(BLACK|...|WHITE)_I
    return true;  // both thickened
  }
  // normal independent controls
  if (cfg.bold_as_colour) {
    if (ansi)
      *pfgi |= 8;
    else  // default
      *pfgi |= 1;  // (FG|BG)_COLOUR_I -> BOLD_(FG|BG)_COLOUR_I
  }
  return cfg.bold_as_font;
}

// removes default colours if not reversed, returns true if still needs thickening
static bool
rtf_bold_decolour(cattrflags attr, colour_i * pfgi, colour_i * pbgi)
{
  bool bold_thickens = cfg.bold_as_font;  // all colours
  // if not reverse:
  // - ATTR_BOLD always thickens default fg on default bg
  // - don't colour default fg/bg (caller interprets COLOUR_NUM as "no colour").
  if (!(attr & ATTR_REVERSE)) {
    if (CCL_DEFAULT(*pfgi) && CCL_DEFAULT(*pbgi))
      bold_thickens = true;  // even if bold_as_font=no
    if (CCL_DEFAULT(*pfgi))
      *pfgi = COLOUR_NUM;  // no colouring
    if (CCL_DEFAULT(*pbgi))
      *pbgi = COLOUR_NUM;  // no colouring
  }
  return (attr & ATTR_BOLD) && bold_thickens;
}

// Applies attributes to the fg/bg colours and returns the new cattr.
//
// "mode" maps to arbitrary sets of "things to do". Mostly these are just
// groups of attributes to handle, but it may also reflect custom needs.
//
// While {FG|BG}MASK and truefg/truebg might update, attributes remain the same.
// The exception is bold which can be applied as colour and/or thickness,
// so ATTR_BOLD bit will be turned off if further thickening should not happen.
// Always returns true colour, except ACM_RTF* modes which only do the palette.
cattr
apply_attr_colour(cattr a, attr_colour_mode mode)
{
  // indexed modifications
  bool do_reverse_i = mode & (ACM_RTF_PALETTE | ACM_RTF_GEN);
  bool do_bold_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN | ACM_SIMPLE | ACM_VBELL_BG);
  bool do_blink_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN);
  bool do_finalize_rtf_i = mode & (ACM_RTF_PALETTE | ACM_RTF_GEN);
  bool do_rtf_bold_decolour_i = mode & (ACM_RTF_GEN);

  colour_i fgi = (a.attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
  colour_i bgi = (a.attr & ATTR_BGMASK) >> ATTR_BGSHIFT;
  a.attr &= ~(ATTR_FGMASK | ATTR_BGMASK);  // we'll refill it later

  if (do_reverse_i && (a.attr & ATTR_REVERSE)) {
    colour_i t = fgi; fgi = bgi; bgi = t;
    colour tmp = a.truefg; a.truefg = a.truebg; a.truebg = tmp;
  }

  bool reset_bold = false;
  if (do_bold_i && (a.attr & ATTR_BOLD))    // rtf_bold_decolour uses ATTR_BOLD
    reset_bold = !apply_bold_colour(&fgi);  // we'll reset afterwards if needed

  if (do_blink_i && (a.attr & ATTR_BLINK)) {
    if (CCL_ANSI8(bgi))
      bgi |= 8;
    else if (CCL_DEFAULT(bgi))
      bgi |= 1;
  }

  if (do_finalize_rtf_i) {
    if (do_rtf_bold_decolour_i) {  // uses ATTR_BOLD, ATTR_REVERSE
      bool thicken = rtf_bold_decolour(a.attr, &fgi, &bgi);
      if (!thicken)
        a.attr &= ~ATTR_BOLD;
    }

    if (a.attr & ATTR_INVISIBLE) {
      fgi = bgi; a.truefg = a.truebg;
    }

    a.attr |= fgi << ATTR_FGSHIFT | bgi << ATTR_BGSHIFT;
    return a;  // rtf colouring prefers indexed where possible
  }

  if (reset_bold)
    a.attr &= ~ATTR_BOLD;  // off if we should not further thicken

  // from here onward the result is true colour
  bool do_dim = mode & (ACM_TERM | ACM_SIMPLE | ACM_VBELL_BG);
  bool do_reverse = mode & (ACM_TERM);
  bool do_invisible = mode & (ACM_TERM | ACM_SIMPLE);
  bool do_vbell_bg = mode & (ACM_VBELL_BG);

  colour fg = fgi >= TRUE_COLOUR ? a.truefg : win_get_colour(fgi);
  colour bg = bgi >= TRUE_COLOUR ? a.truebg : win_get_colour(bgi);

  if (do_dim && (a.attr & ATTR_DIM)) {
    // we dim by blending fg 50-50 with the default terminal bg
    // (x & 0xFEFEFEFE) >> 1  halves each of the RGB components of x .
    // win_get_colour(..) takes term.rvideo into account.
    fg = ((fg & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
  }

  if (do_reverse && (a.attr & ATTR_REVERSE)) {
    colour t = fg; fg = bg; bg = t;
  }

  if (do_invisible && (a.attr & ATTR_INVISIBLE))
    fg = bg;

  if (do_vbell_bg)  // FIXME: we should have TATTR_VBELL. selection should too
    bg = brighten(bg, fg);

  // ACM_TERM does also search and cursor colours. for now we don't handle those

  a.truefg = fg;
  a.truebg = bg;
  a.attr |= TRUE_COLOUR << ATTR_FGSHIFT | TRUE_COLOUR << ATTR_BGSHIFT;
  return a;
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void
win_text(int x, int y, wchar *text, int len, cattr attr, cattr *textattr, ushort lattr, bool has_rtl)
{
  int graph = (attr.attr >> ATTR_GRAPH_SHIFT) & 0xFF;
  int findex = (attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  struct fontfam * ff = &fontfamilies[findex];

  bool clearpad = lattr & LATTR_CLEARPAD;
  trace_line("win_text:", text, len);

  lattr &= LATTR_MODE;
  int char_width = cell_width * (1 + (lattr != LATTR_NORM));

 /* Only want the left half of double width lines */
  // check this before scaling up x to pixels!
  if (lattr != LATTR_NORM && x * 2 >= term.cols)
    return;

 /* Convert to window coordinates */
  x = x * char_width + PADDING;
  y = y * cell_height + PADDING;

  if (attr.attr & ATTR_WIDE)
    char_width *= 2;

  attr = apply_attr_colour(attr, ACM_TERM);
  colour fg = attr.truefg;
  colour bg = attr.truebg;
  // ATTR_BOLD is now set if and only if we need further thickening.

  bool has_cursor = attr.attr & (TATTR_ACTCURS | TATTR_PASCURS);
  colour cursor_colour = 0;

  if (attr.attr & (TATTR_CURRESULT | TATTR_CURMARKED)) {
    bg = cfg.search_current_colour;
    fg = cfg.search_fg_colour;
  }
  else if (attr.attr & (TATTR_RESULT | TATTR_MARKED)) {
    bg = cfg.search_bg_colour;
    fg = cfg.search_fg_colour;
  }

  if (has_cursor) {
    cursor_colour = colours[ime_open ? IME_CURSOR_COLOUR_I : CURSOR_COLOUR_I];

    //static uint mindist = 32768;
    static uint mindist = 22222;
    //static uint mindist = 8000;
    bool too_close = colour_dist(cursor_colour, bg) < mindist;

    if (too_close) {
      //cursor_colour = fg;
      colour ccfg = brighten(cursor_colour, fg);
      colour ccbg = brighten(cursor_colour, bg);
      if (colour_dist(ccfg, bg) < mindist
          && colour_dist(ccfg, bg) < colour_dist(ccbg, bg))
        cursor_colour = ccbg;
      else
        cursor_colour = ccfg;
    }

    if ((attr.attr & TATTR_ACTCURS) && term_cursor_type() == CUR_BLOCK) {
      fg = colours[CURSOR_TEXT_COLOUR_I];
      if (too_close && colour_dist(cursor_colour, fg) < mindist)
        fg = bg;
      bg = cursor_colour;
#ifdef debug_cursor
      printf("set cursor (colour %06X) @(row %d col %d) cursor_on %d\n", bg, (y - PADDING) / cell_height, (x - PADDING) / char_width, term.cursor_on);
#endif
    }
  }

 /* Now that attributes are sorted out, select proper font */
  uint nfont;
  switch (lattr) {
    when LATTR_NORM: nfont = 0;
    when LATTR_WIDE: nfont = FONT_WIDE;
    otherwise:       nfont = FONT_WIDE + FONT_HIGH;
  }

  if (attr.attr & ATTR_EXPAND)
    nfont |= FONT_WIDE;
  else
  if (attr.attr & ATTR_NARROW)
    nfont |= FONT_NARROW;

#ifdef debug_bold
  wchar t[len + 1]; wcsncpy(t, text, len); t[len] = 0;
  printf("bold_mode %d attr_bold %d <%ls>\n", ff->bold_mode, !!(attr.attr & ATTR_BOLD), t);
#endif
  if (ff->bold_mode == BOLD_FONT && (attr.attr & ATTR_BOLD))
    nfont |= FONT_BOLD;
  if (ff->und_mode == UND_FONT && (attr.attr & ATTR_UNDER))
    nfont |= FONT_UNDERLINE;
  if (attr.attr & ATTR_ITALIC)
    nfont |= FONT_ITALIC;
  if (attr.attr & ATTR_STRIKEOUT
      && !cfg.underl_manual && cfg.underl_colour == (colour)-1)
    nfont |= FONT_STRIKEOUT;
  if (attr.attr & TATTR_ZOOMFULL)
    nfont |= FONT_ZOOMFULL;
  another_font(ff, nfont);

  bool force_manual_underline = false;
  if (!ff->fonts[nfont]) {
    if (nfont & FONT_UNDERLINE)
      force_manual_underline = true;
    // Don't force manual bold, it could be bad news.
    nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
  }
  if ((nfont & (FONT_WIDE | FONT_NARROW)) == (FONT_WIDE | FONT_NARROW))
    nfont &= ~(FONT_WIDE | FONT_NARROW);
  another_font(ff, nfont);
  if (!ff->fonts[nfont])
    nfont = FONT_NORMAL;

 /* With selected font, begin preparing the rendering */
  SelectObject(dc, ff->fonts[nfont]);
  SetTextColor(dc, fg);
  SetBkColor(dc, bg);

#define dont_debug_missing_glyphs
#ifdef debug_missing_glyphs
  ushort glyph[len];
  GetGlyphIndicesW(dc, text, len, glyph, true);
  for (int i = 0; i < len; i++)
    if (glyph[i] == 0xFFFF)
      printf(" %04X -> no glyph\n", text[i]);
#endif

 /* Check whether the text has any right-to-left characters */
#ifdef check_rtl_here
#warning now passed as a parameter to avoid redundant checking
  bool has_rtl = false;
  for (int i = 0; i < len && !has_rtl; i++)
    has_rtl = is_rtl(text[i]);
#endif

  uint eto_options = ETO_CLIPPED;
  if (has_rtl) {
   /* We've already done right-to-left processing in the screen buffer,
    * so stop Windows from doing it again (and hence undoing our work).
    * Don't always use this path because GetCharacterPlacement doesn't
    * do Windows font linking.
    */
    char classes[len];
    memset(classes, GCPCLASS_NEUTRAL, len);

    GCP_RESULTSW gcpr = {
      .lStructSize = sizeof(GCP_RESULTSW),
      .lpClass = (void *)classes,
      .lpGlyphs = text,
      .nGlyphs = len
    };

    trace_line(" <ChrPlc:", text, len);
    GetCharacterPlacementW(dc, text, len, 0, &gcpr,
                           FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);
    len = gcpr.nGlyphs;
    trace_line(" >ChrPlc:", text, len);
    eto_options |= ETO_GLYPH_INDEX;
  }


  bool combining = attr.attr & TATTR_COMBINING;
  bool combining_double = attr.attr & TATTR_COMBDOUBL;

  bool let_windows_combine = false;
  if (combining) {
   /* Substitute combining characters by overprinting lookalike glyphs */
    for (int i = 0; i < len; i++)
      text[i] = combsubst(text[i]);
   /* Determine characters that should be combined by Windows */
    if (len == 2) {
      if (text[0] == 'i' && (text[1] == 0x030F || text[1] == 0x0311))
        let_windows_combine = true;
     /* Enforce separate combining characters display if colours differ */
      if (!termattrs_equal_fg(&textattr[1], &attr))
        let_windows_combine = false;
    }
  }

  if (graph && !(graph & 0x80)) {
    for (int i = 0; i < len; i++)
      text[i] = ' ';
  }

 /* Array with offsets between neighbouring characters */
  int dxs[len];
  int dx = combining ? 0 : char_width;
  for (int i = 0; i < len; i++)
    dxs[i] = dx;

  int ulen = 0;
  for (int i = 0; i < len; i++) {
    ulen++;
    if (char1ulen(&text[i]) == 2)
      i++;  // skip low surrogate;
  }
  int width = char_width * (combining ? 1 : ulen);
  RECT box = {
    .top = y, .bottom = y + cell_height,
    .left = x, .right = min(x + width, cell_width * term.cols + PADDING)
  };
  if (attr.attr & ATTR_ITALIC) {
    box.right += cell_width;
    //box.left -= cell_width;
  }
  if (clearpad)
    box.right += PADDING;
  RECT box2 = box;
  if (combining_double)
    box2.right += char_width;


 /* Uniscribe handling */
  use_uniscribe = cfg.font_render == FR_UNISCRIBE && !has_rtl;
  if (combining_double)
    use_uniscribe = false;
  if (use_uniscribe) {
    use_uniscribe = false;
    for (int i = 0; i < len; i++)
      if (text[i] >= 0x80) {
        use_uniscribe = true;
        break;
      }
  }

 /* Begin text output */
  int yt = y + (ff->row_spacing / 2) - (lattr == LATTR_BOT ? cell_height : 0);
  int xt = x + (ff->col_spacing / 2);
  if (attr.attr & TATTR_ZOOMFULL) {
    yt -= ff->row_spacing / 2;
    xt = x;
  }

  int line_width = (3
                    + (attr.attr & ATTR_BOLD ? 1 : 0)
                    + (lattr >= LATTR_WIDE ? 2 : 0)
                    + (lattr >= LATTR_TOP ? 2 : 0)
                   ) * cell_height / 40;
  if (line_width < 1)
    line_width = 1;

 /* DEC Tech adjustments */
  if (graph & 0x80) {  // DEC Technical rendering to be fixed
    if ((graph & ~1) == 0x88)  // left square bracket corners
      xt += line_width + 1;
    else if ((graph & ~1) == 0x8A)  // right square bracket corners
      xt -= line_width + 1;
    else if (graph == 0x87)  // middle angle: don't display ╳, draw (below)
      for (int i = 0; i < len; i++)
        text[i] = ' ';
    else if (graph == 0x80)  // square root base: rather draw (partially)
      for (int i = 0; i < len; i++)
        text[i] = 0x2502;
  }

 /* Determine shadow/overstrike bold or double-width/height width */
  int xwidth = 1;
  if (ff->bold_mode == BOLD_SHADOW && (attr.attr & ATTR_BOLD)) {
    // This could be scaled with font size, but at risk of clipping
    xwidth = 2;
    if (lattr != LATTR_NORM) {
      xwidth = 3; // 4?
    }
  }

 /* Finally, draw the text */
  SetBkMode(dc, OPAQUE);
  uint overwropt = ETO_OPAQUE;
  if (attr.attr & ATTR_ITALIC) {
    SetBkMode(dc, TRANSPARENT);
    overwropt = 0;
  }
  trace_line(" TextOut:", text, len);
  // The combining characters separate rendering trick *alone* 
  // makes some combining characters better (~#553, #295), 
  // others worse (#565); however, together with the 
  // substitute combining characters trick it seems to be the best 
  // workaround for combining characters rendering issues.
  // Yet disabling it for some (heuristically determined) cases:
  if (let_windows_combine)
    combining = false;  // disable separate combining characters display

  if (combining || combining_double)
    *dxs = char_width;  // convince Windows to apply font underlining
  text_out_start(dc, text, len, dxs);

  for (int xoff = 0; xoff < xwidth; xoff++)
    if (combining || combining_double) {
      // Workaround for mangled display of combining characters;
      // Arabic shaping should not be affected as the transformed 
      // presentation forms are not combining characters anymore at this point.
      // Repeat the workaround for bold/wide below.

      if (xoff)
        // restore base character colour in case of distinct combining colours
        SetTextColor(dc, fg);

      // base character
      int ulen = char1ulen(text);
      text_out(dc, xt + xoff, yt, eto_options | overwropt, &box, text, ulen, dxs);

      if (overwropt) {
        SetBkMode(dc, TRANSPARENT);
        overwropt = 0;
      }
      // combining characters
      textattr[0] = attr;
      for (int i = ulen; i < len; i += ulen) {
        // separate stacking of combining characters 
        // does not work with Uniscribe
        use_uniscribe = false;

        int xx = xt + xoff;
        if (combining_double && combiningdouble(text[i]))
          xx += char_width / 2;
        if (!termattrs_equal_fg(&textattr[i], &textattr[i - 1])) {
          // determine colour to be used for combining characters
          colour fg = apply_attr_colour(textattr[i], ACM_SIMPLE).truefg;
          SetTextColor(dc, fg);
        }
        ulen = char1ulen(&text[i]);
        text_out(dc, xx, yt, eto_options, &box2, &text[i], ulen, &dxs[i]);
      }
    }
    else {
      text_out(dc, xt + xoff, yt, eto_options | overwropt, &box, text, len, dxs);
      if (overwropt) {
        SetBkMode(dc, TRANSPARENT);
        overwropt = 0;
      }
    }
  text_out_end();

  // line_width already set above for DEC Tech adjustments

#define dont_debug_vt100_line_drawing_chars
#ifdef debug_vt100_line_drawing_chars
  fg = 0x00FF0000;
#endif

#if __GNUC__ >= 5
#define DRAW_HORIZ 0b1010
#define DRAW_LEFT  0b1000
#define DRAW_RIGHT 0b0010
#define DRAW_VERT  0b0101
#define DRAW_UP    0b0001
#define DRAW_DOWN  0b0100
#else // < 4.3
#define DRAW_HORIZ 0xA
#define DRAW_LEFT  0x8
#define DRAW_RIGHT 0x2
#define DRAW_VERT  0x5
#define DRAW_UP    0x1
#define DRAW_DOWN  0x4
#endif

  if (graph & 0x80) {  // DEC Technical characters to be fixed
    if (graph & 0x08) {
      // square bracket corners already repositioned above
    }
    else {  // Sum segments to be (partially) drawn, square root base
      int sum_width = line_width;
      int y0 = (lattr == LATTR_BOT) ? y - cell_height : y;
      int yoff = (cell_height - line_width) * 3 / 5;
      if (lattr >= LATTR_TOP)
        yoff *= 2;
      int xoff = (char_width - line_width) / 2;
      // 0x80 square root base
      // sum segments:
      // 0x81 upper left: add diagonal to bottom
      // 0x82 lower left: add diagonal to top
      // 0x85 upper right: add hook down
      // 0x86 lower right: add hook up
      // 0x87 middle right angle
      int yt, yb;
      int ycellb = y0 + (lattr >= LATTR_TOP ? 2 : 1) * cell_height;
      if (graph & 1) {  // upper segment: downwards
        yt = y0 + yoff;
        yb = ycellb;
      }
      else {  // lower segment: upwards
        yt = y0;
        yb = y0 + yoff;
      }
      int xl = x + xoff;
      int xr = xl;
      if (graph <= 0x82) {  // diagonals
        sum_width ++;
        xl += line_width - 1;
        xr = x + char_width - 1;
        if (graph == 0x82) {
          int xb = xl;
          xl = xr;
          xr = xb;
        }
      }
      // adjustments with scaling pen:
      xl ++; xr ++;
      int x0 = x;
      if (graph & 1) {  // upper segment: downwards
        yt --;
        yb --;
      } else {  // lower segment: upwards
        yt -= 1;
        yb -= 1;
      }
      // special adjustments:
      if (graph == 0x80) {  // square root base
        xl --;
      }
      else if (graph == 0x87) {  // sum middle right angle
      }
      // draw:
      //HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, sum_width, fg));
      HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, line_width, fg));
      sum_width = 1;  // now handled by pen width
      for (int i = 0; i < len; i++) {
        for (int l = 0; l < sum_width; l++) {
          if (graph == 0x80) {  // square root base
            MoveToEx(dc, xl + l, ycellb, null);
            LineTo(dc, xl - (xl - x0) / 2 + l, yb + l);
            LineTo(dc, x0, yb + l);
          }
          else if (graph == 0x87) {  // sum middle right angle
            MoveToEx(dc, x0 + l, y0, null);
            LineTo(dc, xl + l, yt);
            LineTo(dc, x0 + l, yb);
          }
          else {
            MoveToEx(dc, xl + l, yt, null);
            LineTo(dc, xr + l, yb);
          }
        }
        x0 += char_width;
        xl += char_width;
        xr += char_width;
      }
      oldpen = SelectObject(dc, oldpen);
      DeleteObject(oldpen);
    }
  }
  else if (graph >> 4) {  // VT100 horizontal lines ⎺⎻(─)⎼⎽
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
    int yoff = (cell_height - line_width) * (graph >> 4) / 5;
    if (lattr >= LATTR_TOP)
      yoff *= 2;
    if (lattr == LATTR_BOT)
      yoff -= cell_height;
    for (int l = 0; l < line_width; l++) {
      MoveToEx(dc, x, y + yoff + l, null);
      LineTo(dc, x + len * char_width, y + yoff + l);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }
  else if (graph) {  // VT100 box drawing characters ┘┐┌└┼ ─ ├┤┴┬│
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
    int y0 = (lattr == LATTR_BOT) ? y - cell_height : y;
    int yoff = (cell_height - line_width) * 3 / 5;
    if (lattr >= LATTR_TOP)
      yoff *= 2;
    int xoff = (char_width - line_width) / 2;
    for (int i = 0; i < len; i++) {
      if (graph & DRAW_HORIZ) {
        int xl, xr;
        if (graph & DRAW_LEFT)
          xl = x + i * char_width;
        else
          xl = x + i * char_width + xoff;
        if (graph & DRAW_RIGHT)
          xr = x + (i + 1) * char_width;
        else
          xr = x + i * char_width + xoff + line_width;
        for (int l = 0; l < line_width; l++) {
          MoveToEx(dc, xl, y0 + yoff + l, null);
          LineTo(dc, xr, y0 + yoff + l);
        }
      }
      if (graph & DRAW_VERT) {
        int xi = x + i * char_width + xoff;
        int yt, yb;
        if (graph & DRAW_UP)
          yt = y0;
        else
          yt = y0 + yoff;
        if (graph & DRAW_DOWN)
          yb = y0 + (lattr >= LATTR_TOP ? 2 : 1) * cell_height;
        else
          yb = y0 + yoff + line_width;
        for (int l = 0; l < line_width; l++) {
          MoveToEx(dc, xi + l, yt, null);
          LineTo(dc, xi + l, yb);
        }
      }
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

 /* Manual underline */
  colour ul = fg;
  int uloff = ff->descent + (cell_height - ff->descent + 1) / 2;
  if (lattr == LATTR_BOT)
    uloff = ff->descent + (cell_height - ff->descent + 1) / 2;
  uloff += line_width / 2;
  if (uloff >= cell_height)
    uloff = cell_height - 1;

  if (cfg.underl_colour != (colour)-1)
    ul = cfg.underl_colour;
#ifdef debug_underline
  if (cfg.underl_colour == (colour)-1)
    ul = 0x802020E0;
  if (lattr == LATTR_TOP)
    ul = 0x80E0E020;
  if (lattr == LATTR_BOT)
    ul = 0x80E02020;
#endif
#ifdef debug_bold
  if (xwidth > 1) {
    force_manual_underline = true;
    ul = 0x80E02020;
  }
  else if (nfont & FONT_BOLD) {
    force_manual_underline = true;
    ul = 0x8020E020;
  }
#endif

 /* Underline */
  if (lattr != LATTR_TOP &&
      (force_manual_underline ||
       (ff->und_mode == UND_LINE && (attr.attr & ATTR_UNDER)) ||
       (attr.attr & ATTR_DOUBLYUND))) {
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    int gapfrom = 0, gapdone = 0;
    if (attr.attr & ATTR_DOUBLYUND) {
      if (line_width < 3)
        line_width = 3;
      int gap = line_width / 3;
      gapfrom = (line_width - gap) / 2;
      gapdone = line_width - gapfrom;
    }
    for (int l = 0; l < line_width; l++) {
      if (l >= gapdone || l < gapfrom) {
        MoveToEx(dc, x, y + uloff - l, null);
        LineTo(dc, x + len * char_width, y + uloff - l);
      }
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

 /* Strikeout */
  if (attr.attr & ATTR_STRIKEOUT
      && (cfg.underl_manual || cfg.underl_colour != (colour)-1)) {
    int soff = (ff->descent + (ff->row_spacing / 2)) * 2 / 3;
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    for (int l = 0; l < line_width; l++) {
      MoveToEx(dc, x, y + soff + l, null);
      LineTo(dc, x + len * char_width, y + soff + l);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

 /* Overline */
  if (lattr != LATTR_BOT && attr.attr & ATTR_OVERL) {
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    for (int l = 0; l < line_width; l++) {
      MoveToEx(dc, x, y + l, null);
      LineTo(dc, x + len * char_width, y + l);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

  show_curchar_info('w');
  if (has_cursor) {
#if defined(debug_cursor) && debug_cursor > 1
    printf("painting cursor_type '%c' cursor_on %d\n", "?b_l"[term_cursor_type()+1], term.cursor_on);
#endif
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, cursor_colour));
    switch (term_cursor_type()) {
      when CUR_BLOCK:
        if (attr.attr & TATTR_PASCURS) {
          HBRUSH oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
          Rectangle(dc, x, y, x + char_width, y + cell_height);
          SelectObject(dc, oldbrush);
        }
      when CUR_LINE: {
        int caret_width = 1;
        SystemParametersInfo(SPI_GETCARETWIDTH, 0, &caret_width, 0);
        if (caret_width > char_width)
          caret_width = char_width;
        if (attr.attr & TATTR_RIGHTCURS)
          x += char_width - caret_width;
        if (attr.attr & TATTR_ACTCURS) {
          HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(cursor_colour));
          Rectangle(dc, x, y, x + caret_width, y + cell_height);
          DeleteObject(SelectObject(dc, oldbrush));
        }
        else if (attr.attr & TATTR_PASCURS) {
          for (int dy = 0; dy < cell_height; dy += 2)
            Polyline(
              dc, (POINT[]){{x, y + dy}, {x + caret_width, y + dy}}, 2);
        }
      }
      when CUR_UNDERSCORE:
        y = yt + min(ff->descent, cell_height - 2);
        y += ff->row_spacing * 3 / 8;
        if (lattr >= LATTR_TOP) {
          y += ff->row_spacing / 2;
          if (lattr == LATTR_BOT)
            y += cell_height;
        }
        if (attr.attr & TATTR_ACTCURS)
          Rectangle(dc, x, y, x + char_width, y + 2);
        else if (attr.attr & TATTR_PASCURS) {
          for (int dx = 0; dx < char_width; dx += 2) {
            SetPixel(dc, x + dx, y, cursor_colour);
            SetPixel(dc, x + dx, y + 1, cursor_colour);
          }
        }
    }
    DeleteObject(SelectObject(dc, oldpen));
  }
}

/* Check availability of characters in the current font.
 * Zeroes each of the characters in the input array that isn't available.
 */
void
win_check_glyphs(wchar *wcs, uint num)
{
  int findex = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  struct fontfam * ff = &fontfamilies[findex];

  bool bold = (ff->bold_mode == BOLD_FONT) && (term.curs.attr.attr & ATTR_BOLD);
  bool italic = term.curs.attr.attr & ATTR_ITALIC;
  HFONT f = ff->fonts[(bold ? FONT_BOLD : FONT_NORMAL) | italic ? FONT_ITALIC : 0];
  if (!f)  // may not have been initialized
    f = ff->fonts[FONT_NORMAL];

  HDC dc = GetDC(wnd);
  SelectObject(dc, f);
  ushort glyphs[num];
  GetGlyphIndicesW(dc, wcs, num, glyphs, true);
  for (size_t i = 0; i < num; i++) {
    if (glyphs[i] == 0xFFFF || glyphs[i] == 0x1F)
      wcs[i] = 0;
  }
  ReleaseDC(wnd, dc);
}

/* This function gets the actual width of a character in the normal font.
   Usage:
   * determine whether to trim an ambiguous wide character 
     (of a CJK ambiguous-wide font such as BatangChe) to normal width 
     if desired.
   * also whether to expand a normal width character if expected wide
 */
int
win_char_width(xchar c)
{
  int findex = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  struct fontfam * ff = &fontfamilies[findex];

#define win_char_width

#define measure_width

#if ! defined(measure_width) && ! defined(debug_win_char_width)
 /* If the font max width is the same as the font average width
  * then this function is a no-op.
  */
  if (!ff->font_dualwidth)
    // this optimization ignores font fallback and should be dropped 
    // if ever a more particular width checking is implemented (#615)
    return 1;
#endif

 /* Speedup, I know of no font where ASCII is the wrong width */
#ifdef debug_win_char_width
  if (c != 'A')
#endif
  if (c >= ' ' && c <= '~')  // don't width-check ASCII
    return 1;

  bool bold = (ff->bold_mode == BOLD_FONT) && (term.curs.attr.attr & ATTR_BOLD);
  bool italic = term.curs.attr.attr & ATTR_ITALIC;
  int font4index = (bold ? FONT_BOLD : FONT_NORMAL) | italic ? FONT_ITALIC : 0;
  HFONT f = ff->fonts[font4index];
  if (!f) {  // may not have been initialized
    f = ff->fonts[FONT_NORMAL];
    font4index = FONT_NORMAL;
  }

  HDC dc = GetDC(wnd);
#ifdef debug_win_char_width
  bool ok0 = !!
#endif
  SelectObject(dc, f);
#ifdef debug_win_char_width
  if (c == 0x2001)
    win_char_width(0x5555);
  if (!ok0)
    printf("width %04X failed (dc %p)\n", c, dc);
  else if (c > '~' || c == 'A') {
    int cw = 0;
    BOOL ok1 = GetCharWidth32W(dc, c, c, &cw);  // "not on TrueType"
    float cwf = 0.0;
    BOOL ok2 = GetCharWidthFloatW(dc, c, c, &cwf);
    ABC abc; memset(&abc, 0, sizeof abc);
    BOOL ok3 = GetCharABCWidthsW(dc, c, c, &abc);  // only on TrueType
    ABCFLOAT abcf; memset(&abcf, 0, sizeof abcf);
    BOOL ok4 = GetCharABCWidthsFloatW(dc, c, c, &abcf);
    printf("w %04X [cell %d] - 32 %d %d - flt %d %.3f - abc %d %d %d %d - abc flt %d %4.1f %4.1f %4.1f\n", 
           c, cell_width, ok1, cw, ok2, cwf, 
           ok3, abc.abcA, abc.abcB, abc.abcC, 
           ok4, abcf.abcfA, abcf.abcfB, abcf.abcfC);
  }
#endif

  int ibuf = 0;
  bool ok = GetCharWidth32W(dc, c, c, &ibuf);
  if (!ok) {
    ReleaseDC(wnd, dc);
    return 0;
  }

  // report char as wide if its width is more than 1½ cells;
  // this is unreliable if font fallback is involved (#615)
  ibuf += cell_width / 2 - 1;
  ibuf /= cell_width;
  if (ibuf > 1) {
#ifdef debug_win_char_width
    printf("enquired %04X %dpx cell %dpx\n", c, ibuf, cell_width);
#endif
    ReleaseDC(wnd, dc);
    return ibuf;
  }

#ifdef measure_width
  int act_char_width(wchar wc)
  {
    HDC wid_dc = CreateCompatibleDC(dc);
    HBITMAP wid_bm = CreateCompatibleBitmap(dc, cell_width * 2, cell_height);
    HBITMAP wid_oldbm = SelectObject(wid_dc, wid_bm);
    SelectObject(wid_dc, ff->fonts[FONT_NORMAL]);
    SetTextAlign(wid_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    SetTextColor(wid_dc, RGB(255, 255, 255));
    SetBkColor(wid_dc, RGB(0, 0, 0));
    SetBkMode(wid_dc, OPAQUE);
    int dx = 0;
    use_uniscribe = cfg.font_render == FR_UNISCRIBE;
    text_out_start(wid_dc, &wc, 1, &dx);
    text_out(wid_dc, 0, 0, ETO_OPAQUE, null, &wc, 1, &dx);
    text_out_end();
# ifdef debug_win_char_width
    for (int y = 0; y < cell_height; y++) {
      printf("%2d|", y);
      for (int x = 0; x < cell_width * 2; x++) {
        COLORREF c = GetPixel(wid_dc, x, y);
        printf("%c", c != RGB(0, 0, 0) ? '*' : ' ');
      }
      printf("|\n");
    }
# endif

    int wid = 0;
    for (int x = cell_width * 2 - 1; !wid && x >= 0; x--)
      for (int y = 0; y < cell_height; y++) {
        COLORREF c = GetPixel(wid_dc, x, y);
        if (c != RGB(0, 0, 0)) {
          wid = x + 1;
          break;
        }
      }
    SelectObject(wid_dc, wid_oldbm);
    DeleteObject(wid_bm);
    DeleteDC(wid_dc);
    return wid;
  }

  if (c >= 0x2160 && c <= 0x2179) {  // Roman Numerals
    ReleaseDC(wnd, dc);
    return 2;
  }
  if (c >= 0x2500 && c <= 0x257F) {  // Box Drawing
    ReleaseDC(wnd, dc);
    return 2;  // do not stretch; vertical lines might get pushed out of cell
  }
  if ((c >= 0x2580 && c <= 0x2588) || (c >= 0x2592 && c <= 0x2594)) {
    // Block Elements
    ReleaseDC(wnd, dc);
    return 1;  // should be stretched to fill whole cell
               // does not have the desired effect, 
               // although FONT_WIDE is actually activated
  }

  if (ambigwide(c) &&
#ifdef check_ambig_non_letters
#warning instead we now check all non-letters with some exclusions
      (c == 0x20AC  // €
      || (c >= 0x2100 && c <= 0x23FF)   // Letterlike, Number Forms, Arrows, Math Operators, Misc Technical
      || (c >= 0x2460 && c <= 0x24FF)   // Enclosed Alphanumerics
      || (c >= 0x25A0 && c <= 0x25FF)   // Geometric Shapes
      || (c >= 0x2600 && c <= 0x27BF)   // Miscellaneous Symbols, Dingbats
      || (c >= 0x2B00 && c <= 0x2BFF)   // Miscellaneous Symbols and Arrows
      || (c >= 0x1F100 && c <= 0x1F1FF) // Enclosed Alphanumeric Supplement
      )
#else
      // check all non-letters
      (bidi_class(c) != L               // indicates not a letter
      || (c >= 0x249C && c <= 0x24E9)   // parenthesized/circled letters
      || (c >= 0x3248 && c <= 0x324F)   // Enclosed CJK Letters and Months
      || (c >= 0x1F110 && c <= 0x1F12A) // Enclosed Alphanumeric Supplement
      )
      &&
      // with some exceptions
      !(  (c >= 0x2500 && c <= 0x2588)  // Box Drawing, Block Elements
       || (c >= 0x2592 && c <= 0x2594)  // Block Elements
       || (c >= 0x2160 && c <= 0x2179)  // Roman Numerals
       //|| wcschr (W("‐‑‘’‚‛“”„‟‹›"), c) // #712 workaround; now caching
       )
#endif
     ) {
    // look up c in charpropcache
    struct charpropcache * cpfound = 0;
    for (uint i = 0; i < ff->cpcachelen[font4index]; i++)
      if (ff->cpcache[font4index][i].ch == c) {
        if (ff->cpcache[font4index][i].width) {
          ReleaseDC(wnd, dc);
          return ff->cpcache[font4index][i].width;
        }
        else {
          // cached (e.g. by win_check_glyphs) but not measured
          cpfound = &ff->cpcache[font4index][i];
        }
      }

    int mbuf = act_char_width(c);
    // report char as wide if its measured width is more than 1½ cells
    int width = mbuf > cell_width ? 2 : 1;
    ReleaseDC(wnd, dc);
# ifdef debug_win_char_width
    if (c > '~' || c == 'A') {
      printf("measured %04X %dpx cell %dpx width %d\n", c, mbuf, cell_width, width);
    }
# endif
    // cache width
    if (cpfound)
      cpfound->width = width;
    else {
      // max size per cache 138739 as of Unicode 10.0;
      // we should perhaps limit this...
      struct charpropcache * newcpcache = renewn(ff->cpcache[font4index], ff->cpcachelen[font4index] + 1);
      if (newcpcache) {
        ff->cpcache[font4index] = newcpcache;
        ff->cpcache[font4index][ff->cpcachelen[font4index]].ch = c;
        ff->cpcache[font4index][ff->cpcachelen[font4index]].width = width;
        ff->cpcachelen[font4index]++;
      }
    }
    return width;
  }
#endif

  ReleaseDC(wnd, dc);
  return ibuf;
}

#define dont_debug_win_combine

/* Try to combine a base and combining character into a precomposed one.
 * Returns 0 if unsuccessful.
 */
wchar
win_combine_chars(wchar c, wchar cc)
{
  wchar cs[2];
  int len = FoldStringW(MAP_PRECOMPOSED, (wchar[]){c, cc}, 2, cs, 2);
  if (len == 1) {  // check whether the combined glyph exists
    ushort glyph;
    HDC dc = GetDC(wnd);
    GetGlyphIndicesW(dc, cs, 1, &glyph, true);
    ReleaseDC(wnd, dc);
#ifdef debug_win_combine
    printf("win_combine %04X %04X -> %04X\n", c, cc, glyph == 0xFFFF ? 0 : *cs);
#endif
    if (glyph == 0xFFFF)
      return 0;
    else
      return *cs;
  }
  else
    return 0;
}


// Colour settings

void
win_set_colour(colour_i i, colour c)
{
  if (i >= COLOUR_NUM)
    return;

  static bool bold_colour_selected = false;

  if (c == (colour)-1) {
    // ... reset to default ...
    if (i == BOLD_FG_COLOUR_I) {
      bold_colour_selected = false;
      if (cfg.bold_colour != (colour)-1)
        colours[BOLD_FG_COLOUR_I] = cfg.bold_colour;
      else
        colours[BOLD_FG_COLOUR_I] = brighten(colours[FG_COLOUR_I], colours[BG_COLOUR_I]);
    }
    else if (i == FG_COLOUR_I)
      colours[i] = cfg.fg_colour;
    else if (i == BG_COLOUR_I)
      colours[i] = cfg.bg_colour;
    else if (i == CURSOR_COLOUR_I)
      colours[i] = cfg.cursor_colour;
    else if (i == SEL_COLOUR_I)
      colours[i] = cfg.sel_bg_colour;
    else if (i == SEL_TEXT_COLOUR_I)
      colours[i] = cfg.sel_fg_colour;

    return;
  }

  colours[i] = c;
  if (i < 16)
    colours[i + ANSI0] = c;

#ifdef debug_brighten
  printf("colours[%d] = %06X\n", i, c);
#endif

  switch (i) {
    when FG_COLOUR_I:
      // should we make this conditional, 
      // unless bold colour has been set explicitly?
      if (!bold_colour_selected) {
        if (cfg.bold_colour != (colour)-1)
          colours[BOLD_FG_COLOUR_I] = cfg.bold_colour;
        else {
          colours[BOLD_FG_COLOUR_I] = brighten(c, colours[BG_COLOUR_I]);
          // renew this too as brighten() may refer to contrast colour:
          colours[BOLD_BG_COLOUR_I] = brighten(colours[BG_COLOUR_I], colours[FG_COLOUR_I]);
        }
      }
    when BOLD_FG_COLOUR_I:
      bold_colour_selected = true;
    when BG_COLOUR_I:
      if (!bold_colour_selected) {
        if (cfg.bold_colour != (colour)-1)
          colours[BOLD_FG_COLOUR_I] = cfg.bold_colour;
        else {
          colours[BOLD_BG_COLOUR_I] = brighten(c, colours[FG_COLOUR_I]);
          // renew this too as brighten() may refer to contrast colour:
          colours[BOLD_FG_COLOUR_I] = brighten(colours[FG_COLOUR_I], colours[BG_COLOUR_I]);
        }
      }
    when CURSOR_COLOUR_I: {
      // Set the colour of text under the cursor to whichever of foreground
      // and background colour is further away from the cursor colour.
      colour fg = colours[FG_COLOUR_I], bg = colours[BG_COLOUR_I];
      colours[CURSOR_TEXT_COLOUR_I] =
        colour_dist(c, fg) > colour_dist(c, bg) ? fg : bg;
      colours[IME_CURSOR_COLOUR_I] = c;
    }
    otherwise:
      break;
  }
  // Redraw everything.
  win_invalidate_all();
}

colour
win_get_colour(colour_i i)
{
  if (term.rvideo && CCL_DEFAULT(i))
    return colours[i ^ 2];  // [BOLD]_FG_COLOUR_I  <-->  [BOLD]_BG_COLOUR_I
  return i < COLOUR_NUM ? colours[i] : 0;
}

colour
win_get_sys_colour(bool fg)
{
  return GetSysColor(fg ? COLOR_WINDOWTEXT : COLOR_WINDOW);
}

void
win_reset_colours(void)
{
  memcpy(colours, cfg.ansi_colours, sizeof cfg.ansi_colours);
  // duplicate 16 ANSI colours to facilitate distinct handling (implemented)
  // and also distinct colour values if desired
  memcpy(&colours[ANSI0], cfg.ansi_colours, sizeof cfg.ansi_colours);

  // Colour cube
  colour_i i = 16;
  for (uint r = 0; r < 6; r++)
    for (uint g = 0; g < 6; g++)
      for (uint b = 0; b < 6; b++)
        colours[i++] = RGB(r ? r * 40 + 55 : 0,
                           g ? g * 40 + 55 : 0,
                           b ? b * 40 + 55 : 0);

  // Grayscale
  for (uint s = 0; s < 24; s++) {
    uint c = s * 10 + 8;
    colours[i++] = RGB(c, c, c);
  }

  // Foreground, background, cursor
  win_set_colour(FG_COLOUR_I, cfg.fg_colour);
  win_set_colour(BG_COLOUR_I, cfg.bg_colour);
  win_set_colour(CURSOR_COLOUR_I, cfg.cursor_colour);
  if (cfg.ime_cursor_colour != DEFAULT_COLOUR)
    win_set_colour(IME_CURSOR_COLOUR_I, cfg.ime_cursor_colour);
  win_set_colour(SEL_COLOUR_I, cfg.sel_bg_colour);
  win_set_colour(SEL_TEXT_COLOUR_I, cfg.sel_fg_colour);
}
