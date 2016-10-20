// wintext.c (part of mintty)
// Copyright 2008-13 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"
#include "charset.h"  // wcscpy, combiningdouble

#include "minibidi.h"
#include "winimg.h"

#include <winnls.h>


enum {
  FONT_NORMAL    = 0x00,
  FONT_BOLD      = 0x01,
  FONT_UNDERLINE = 0x02,
  FONT_BOLDUND   = FONT_BOLD | FONT_UNDERLINE,
  FONT_ITALIC    = 0x04,
  FONT_STRIKEOUT = 0x08,
  FONT_HIGH      = 0x10,
  FONT_WIDE      = 0x20,
  FONT_NARROW    = 0x40,
  FONT_MAXNO     = FONT_WIDE + FONT_NARROW
};

LOGFONT lfont;
static HFONT fonts[FONT_MAXNO];
static bool fontflag[FONT_MAXNO];
static int fw_norm = FW_NORMAL;
static int fw_bold = FW_BOLD;
static int row_spacing;

enum {LDRAW_CHAR_NUM = 31, LDRAW_CHAR_TRIES = 4};

// VT100 linedraw character mappings for current font.
wchar win_linedraw_chars[LDRAW_CHAR_NUM];

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

static enum {/*unused*/BOLD_NONE, BOLD_SHADOW, BOLD_FONT} bold_mode;
static enum {UND_LINE, UND_FONT} und_mode;
static int descent;

// Current font size (with any zooming)
int font_size;  // logical font size, as configured (< 0: pixel size)

// Font screen dimensions
int cell_width, cell_height;  // includes spacing
static int font_height;  // pure font height, without spacing
int PADDING = 1;
static bool font_dualwidth;

bool font_ambig_wide;

COLORREF colours[COLOUR_NUM];
static bool bold_colour_selected = false;

static uint
colour_dist(colour a, colour b)
{
  return
    2 * sqr(red(a) - red(b)) +
    4 * sqr(green(a) - green(b)) +
    1 * sqr(blue(a) - blue(b));
}

#define dont_debug_brighten

static colour
brighten(colour c, colour against)
{
  uint r = red(c), g = green(c), b = blue(c);
  // "brighten" away from the background:
  // if we are closer to black than the contrast reference, rather darken
  bool darken = colour_dist(c, 0) < colour_dist(against, 0);
#ifdef debug_brighten
  printf ("%s %06X against %06X\n", darken ? "darkening" : "brighting", c, against);
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
    printf ("darker %06X -> %06X dist %d\n", c, bright, colour_dist(c, bright));
#endif
    if (colour_dist(bright, c) < thrsh || colour_dist(bright, against) < thrsh) {
      bright = _brighter();
#ifdef debug_brighten
      printf ("   fix %06X -> %06X dist %d/%d\n", c, bright, colour_dist(bright, c), colour_dist(bright, against));
#endif
    }
  }
  else {
    bright = _brighter();
#ifdef debug_brighten
    printf ("lightr %06X -> %06X dist %d\n", c, bright, colour_dist(c, bright));
#endif
    if (colour_dist(bright, c) < thrsh || colour_dist(bright, against) < thrsh) {
      bright = _darker();
#ifdef debug_brighten
      printf ("   fix %06X -> %06X dist %d/%d\n", c, bright, colour_dist(bright, c), colour_dist(bright, against));
#endif
    }
  }

  return bright;
}

static uint
get_font_quality(void) {
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
create_font(int weight, bool underline)
{
#ifdef debug_create_font
  printf("font [??]: %d (size %d) 0 w%4d i0 u%d s0\n", font_height, font_size, weight, underline);
#endif
  return
    CreateFontW(
      font_height, 0, 0, 0, weight, false, underline, false,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      get_font_quality(), FIXED_PITCH | FF_DONTCARE,
      cfg.font.name
    );
}

static int
row_padding(int i, int e)
{
  if (i == 0 && e == 0)
    return 0;  // 2 sometimes looks nicer but may break box characters
  else {
    int exc = 0;
    if (i > 3)
      exc = i - 3;
    int adj = e - exc;
    if (adj <= 0)
      return adj;
    else
      return 0;  // return adj may look nicer but break box characters
  }
}

static void
show_msg(wstring msg, wstring title)
{
  if (fprintf(stderr, "%ls", title) < 0 || fputs("\n", stderr) < 0 ||
      fprintf(stderr, "%ls", msg) < 0 || fputs("\n", stderr) < 0 ||
      fflush(stderr) < 0)
    MessageBoxW(0, msg, title, MB_ICONWARNING);
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
adjust_font_weights(void)
{
  LOGFONTW lf;
#if CYGWIN_VERSION_API_MINOR >= 201
  swprintf(lf.lfFaceName, lengthof(lf.lfFaceName), W("%ls"), cfg.font.name);
#else
  if (wcslen(cfg.font.name) < lengthof(lf.lfFaceName))
    wcscpy(lf.lfFaceName, cfg.font.name);
  else
    wcscpy(lf.lfFaceName, W("Lucida Console"));
#endif
  lf.lfPitchAndFamily = 0;
  //lf.lfCharSet = ANSI_CHARSET;   // report only ANSI character range
  // use this to avoid double error popup (e.g. Font=David):
  lf.lfCharSet = DEFAULT_CHARSET;  // report all supported char ranges

  // find the closest available widths such that
  // fw_norm_0 <= fw_norm <= fw_norm_1
  // fw_bold_0 <= fw_bold <= fw_bold_1
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

    if (lfp->lfWeight > fw_norm_0 && lfp->lfWeight <= fw_norm)
      fw_norm_0 = lfp->lfWeight;
    if (lfp->lfWeight > fw_bold_0 && lfp->lfWeight <= fw_bold)
      fw_bold_0 = lfp->lfWeight;
    if (lfp->lfWeight < fw_norm_1 && lfp->lfWeight >= fw_norm)
      fw_norm_1 = lfp->lfWeight;
    if (lfp->lfWeight < fw_bold_1 && lfp->lfWeight >= fw_bold)
      fw_bold_1 = lfp->lfWeight;

    return 1;  // continue
  }

  HDC dc = GetDC(0);
  EnumFontFamiliesExW(dc, &lf, enum_fonts, 0, 0);
  trace_font(("font width (%d)%d(%d)/(%d)%d(%d)", fw_norm_0, fw_norm, fw_norm_1, fw_bold_0, fw_bold, fw_bold_1));
  ReleaseDC(0, dc);

  // check if no font found
  if (!font_found) {
    show_msg(_W("Font not found, using system substitute"), cfg.font.name);
    fw_norm = 400;
    fw_bold = 700;
    trace_font(("//\n"));
    return;
  }
  if (!ansi_found && !cs_found) {
    show_msg(_W("Font has limited support for character ranges"), cfg.font.name);
  }

  // find available widths closest to selected widths
  if (abs(fw_norm - fw_norm_0) <= abs(fw_norm - fw_norm_1) && fw_norm_0 > 0)
    fw_norm = fw_norm_0;
  else if (fw_norm_1 < 1000)
    fw_norm = fw_norm_1;
  if (abs(fw_bold - fw_bold_0) < abs(fw_bold - fw_bold_1) || fw_bold_1 > 1000)
    fw_bold = fw_bold_0;
  else if (fw_bold_1 < 1001)
    fw_bold = fw_bold_1;
  // ensure bold is bolder than normal
  if (fw_bold <= fw_norm) {
    trace_font((" -> %d/%d", fw_norm, fw_bold));
    if (fw_norm_0 < fw_norm && fw_norm_0 > 0)
      fw_norm = fw_norm_0;
    if (fw_bold - fw_norm < 300) {
      if (fw_bold_1 > fw_bold && fw_bold_1 < 1001)
        fw_bold = fw_bold_1;
      else
        fw_bold = min(fw_norm + 300, 1000);
    }
  }
  // enforce preselected boldness
  int selweight = cfg.font.weight;
  if (selweight < 700 && cfg.font.isbold)
    selweight = 700;
  if (selweight - fw_norm >= 300) {
    trace_font((" -> %d(%d)/%d", fw_norm, selweight, fw_bold));
    fw_norm = selweight;
    fw_bold = min(fw_norm + 300, 1000);
  }
  trace_font((" -> %d/%d\n", fw_norm, fw_bold));
}

/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one. The other (potentially) twentyone fonts are done
 * if/when they are needed.
 *
 * We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 *
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 */
void
win_init_fonts(int size)
{
  trace_resize(("--- init_fonts %d\n", size));
  TEXTMETRIC tm;
  int fontsize[3];
  int i;

  font_size = size;

  for (i = 0; i < FONT_MAXNO; i++) {
    if (fonts[i]) {
      DeleteObject(fonts[i]);
      fonts[i] = 0;
    }
    fontflag[i] = 0;
  }

  bold_mode = cfg.bold_as_font ? BOLD_FONT : BOLD_SHADOW;
  und_mode = UND_FONT;

  if (cfg.font.weight) {
    fw_norm = cfg.font.weight;
    fw_bold = min(fw_norm + 300, 1000);
    // adjust selected font weights to available font weights
    trace_font(("-> Weight %d/%d\n", fw_norm, fw_bold));
    adjust_font_weights();
    trace_font(("->     -> %d/%d\n", fw_norm, fw_bold));
  }
  else if (cfg.font.isbold) {
    fw_norm = FW_BOLD;
    fw_bold = FW_HEAVY;
    trace_font(("-> IsBold %d/%d\n", fw_norm, fw_bold));
  }
  else {
    fw_norm = FW_DONTCARE;
    fw_bold = FW_BOLD;
    trace_font(("-> normal %d/%d\n", fw_norm, fw_bold));
  }

  HDC dc = GetDC(wnd);

#ifdef debug_dpi
  printf("dpi %d dev %d\n", dpi, GetDeviceCaps(dc, LOGPIXELSY));
#endif
  if (cfg.handle_dpichanged && per_monitor_dpi_aware)
    font_height =
      size > 0 ? -MulDiv(size, dpi, 72) : -size;
      // dpi is determined initially and via WM_WINDOWPOSCHANGED;
      // if WM_DPICHANGED were used, this would need to be modified
  else
    font_height =
      size > 0 ? -MulDiv(size, GetDeviceCaps(dc, LOGPIXELSY), 72) : -size;

  cell_width = 0;

  fonts[FONT_NORMAL] = create_font(fw_norm, false);

  GetObject(fonts[FONT_NORMAL], sizeof (LOGFONT), &lfont);
  trace_font(("created font %s %ld it %d cs %d\n", lfont.lfFaceName, (long int)lfont.lfWeight, lfont.lfItalic, lfont.lfCharSet));

  SelectObject(dc, fonts[FONT_NORMAL]);
  GetTextMetrics(dc, &tm);
  row_spacing = row_padding(tm.tmInternalLeading, tm.tmExternalLeading);
  trace_font(("h %ld asc %ld dsc %ld ild %ld eld %ld %ls\n", (long int)tm.tmHeight, (long int)tm.tmAscent, (long int)tm.tmDescent, (long int)tm.tmInternalLeading, (long int)tm.tmExternalLeading, cfg.font.name));
  row_spacing += cfg.row_spacing;
  if (row_spacing < -tm.tmDescent)
    row_spacing = -tm.tmDescent;
    trace_font(("row spacing int %ld ext %ld -> %+d; add %+d -> %+d; desc %ld -> %+d %ls\n", 
      (long int)tm.tmInternalLeading, (long int)tm.tmExternalLeading, row_padding(tm.tmInternalLeading, tm.tmExternalLeading),
      cfg.row_spacing, row_padding(tm.tmInternalLeading, tm.tmExternalLeading) + cfg.row_spacing,
      (long int)tm.tmDescent, row_spacing, cfg.font.name));
#ifdef check_charset_only_for_returned_font
  int default_charset = get_default_charset();
  if (tm.tmCharSet != default_charset && default_charset != DEFAULT_CHARSET) {
    show_msg(_W("Font does not support system locale"), cfg.font.name);
  }
#endif

  // to be checked: whether usages of cell_height should include row_spacing
  // (and likewise for cell_width);
  // for font creation, as a workaround, row_spacing is removed again
  cell_height = tm.tmHeight + row_spacing;
  cell_width = tm.tmAveCharWidth + cfg.col_spacing;
  font_dualwidth = (tm.tmMaxCharWidth >= tm.tmAveCharWidth * 3 / 2);
  PADDING = tm.tmAveCharWidth;
  if (cfg.padding >= 0 && cfg.padding < PADDING)
    PADDING = cfg.padding;
#ifdef debug_create_font
  printf("size %d -> height %d -> height %d\n", size, font_height, cell_height);
#endif

  // Determine whether ambiguous-width characters are wide in this font */
  float latin_char_width, greek_char_width, line_char_width;
  GetCharWidthFloatW(dc, 0x0041, 0x0041, &latin_char_width);
  GetCharWidthFloatW(dc, 0x03B1, 0x03B1, &greek_char_width);
  GetCharWidthFloatW(dc, 0x2500, 0x2500, &line_char_width);

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
  printf("font ambig %d/%d (dual %d)\n", font_ambig_wide, faw, font_dualwidth);
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
    win_linedraw_chars[i] = linedraw_chars[i][j];
#define draw_vt100_line_drawing_chars
#ifdef draw_vt100_line_drawing_chars
    if ('j' - '`' <= i && i <= 'x' - '`')
      win_linedraw_chars[i] = linedraw_chars[i][0];
#endif
  }

  fonts[FONT_UNDERLINE] = create_font(fw_norm, true);

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
  {
    HDC und_dc;
    HBITMAP und_bm, und_oldbm;

    und_dc = CreateCompatibleDC(dc);
    und_bm = CreateCompatibleBitmap(dc, cell_width, cell_height);
    und_oldbm = SelectObject(und_dc, und_bm);
    SelectObject(und_dc, fonts[FONT_UNDERLINE]);
    SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    SetTextColor(und_dc, RGB(255, 255, 255));
    SetBkColor(und_dc, RGB(0, 0, 0));
    SetBkMode(und_dc, OPAQUE);
    ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, null, " ", 1, null);

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
      trace_font(("ul outbox %ls\n", cfg.font.name));
      und_mode = UND_LINE;
      DeleteObject(fonts[FONT_UNDERLINE]);
      fonts[FONT_UNDERLINE] = 0;
    }
  }

  if (bold_mode == BOLD_FONT)
    fonts[FONT_BOLD] = create_font(fw_bold, false);

  descent = tm.tmAscent + 1;
  if (descent >= cell_height)
    descent = cell_height - 1;

  for (i = 0; i < 3; i++) {
    if (fonts[i]) {
      if (SelectObject(dc, fonts[i]) && GetTextMetrics(dc, &tm))
        fontsize[i] = tm.tmAveCharWidth + 256 * tm.tmHeight;
      else
        fontsize[i] = -i;
    }
    else
      fontsize[i] = -i;
  }

  ReleaseDC(wnd, dc);

  if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
    trace_font(("ul size!= %ls\n", cfg.font.name));
    und_mode = UND_LINE;
    DeleteObject(fonts[FONT_UNDERLINE]);
    fonts[FONT_UNDERLINE] = 0;
  }

  if (bold_mode == BOLD_FONT && fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
    trace_font(("bold_mode %d\n", bold_mode));
    bold_mode = BOLD_SHADOW;
    DeleteObject(fonts[FONT_BOLD]);
    fonts[FONT_BOLD] = 0;
  }
  trace_font(("bold_mode %d\n", bold_mode));
  fontflag[0] = fontflag[1] = fontflag[2] = 1;
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

static void
do_update(void)
{
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
another_font(int fontno)
{
  int basefont;
  int u, w, i, s, x;

  if (fontno < 0 || fontno >= FONT_MAXNO || fontflag[fontno])
    return;

  basefont = (fontno & ~(FONT_BOLDUND));
  if (basefont != fontno && !fontflag[basefont])
    another_font(basefont);

  w = fw_norm;
  i = false;
  s = false;
  u = false;
  x = cell_width;

  if (fontno & FONT_WIDE)
    x *= 2;
  if (fontno & FONT_NARROW)
    x = (x + 1) / 2;
  if (fontno & FONT_BOLD)
    w = fw_bold;
  if (fontno & FONT_ITALIC)
    i = true;
  if (fontno & FONT_STRIKEOUT)
    s = true;
  if (fontno & FONT_UNDERLINE)
    u = true;

#ifdef debug_create_font
  printf("font [%02X]: %d (size %d) %d w%4d i%d u%d s%d\n", fontno, font_height * (1 + !!(fontno & FONT_HIGH)), font_size, x, w, i, u, s);
#endif
  fonts[fontno] =
    // workaround: remove effect of row_spacing from font creation;
    // to be checked: usages of cell_height elsewhere
    CreateFontW(font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w, i, u, s,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                get_font_quality(), FIXED_PITCH | FF_DONTCARE, cfg.font.name);

  fontflag[fontno] = 1;
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

#ifdef debug_win_text

void
trace_line(char * tag, wchar * text, int len)
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

#else
#define trace_line(tag, text, len)	
#endif

static wchar
combsubst(wchar comb)
{
  static const struct {
    wchar comb;
    wchar subst;
  } lookup[] = {
    {0x0300, 0x0060},
    {0x0301, 0x00B4},
    {0x0302, 0x02C6},
    {0x0303, 0x02DC},
    {0x0304, 0x00AF},
    {0x0305, 0x203E},
    {0x0306, 0x02D8},
    {0x0307, 0x02D9},
    {0x0308, 0x00A8},
    {0x030A, 0x02DA},
    {0x030B, 0x02DD},
    {0x030C, 0x02C7},
    {0x0327, 0x00B8},
    {0x0328, 0x02DB},
    {0x0332, 0x005F},
    {0x0333, 0x2017},
    {0x033E, 0x2E2F},
    {0x0342, 0x1FC0},
    {0x0343, 0x1FBD},
    {0x0344, 0x0385},
    {0x0345, 0x037A},
    {0x3099, 0x309B},
    {0x309A, 0x309C},
    {0xA67C, 0xA67E},
    {0xA67D, 0xA67F},
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
    else
      return lookup[k].subst;
  }
  return comb;
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void
win_text(int x, int y, wchar *text, int len, cattr attr, int lattr, bool has_rtl)
{
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
  printf("bold_mode %d attr_bold %d <%ls>\n", bold_mode, !!(attr.attr & ATTR_BOLD), t);
#endif
  if (bold_mode == BOLD_FONT && (attr.attr & ATTR_BOLD))
    nfont |= FONT_BOLD;
  if (und_mode == UND_FONT && (attr.attr & ATTR_UNDER))
    nfont |= FONT_UNDERLINE;
  if (attr.attr & ATTR_ITALIC)
    nfont |= FONT_ITALIC;
  if (attr.attr & ATTR_STRIKEOUT)
    nfont |= FONT_STRIKEOUT;
  another_font(nfont);

  bool force_manual_underline = false;
  if (!fonts[nfont]) {
    if (nfont & FONT_UNDERLINE)
      force_manual_underline = true;
    // Don't force manual bold, it could be bad news.
    nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
  }
  if ((nfont & (FONT_WIDE | FONT_NARROW)) == (FONT_WIDE | FONT_NARROW))
    nfont &= ~(FONT_WIDE | FONT_NARROW);
  another_font(nfont);
  if (!fonts[nfont])
    nfont = FONT_NORMAL;

  colour_i fgi = (attr.attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
  colour_i bgi = (attr.attr & ATTR_BGMASK) >> ATTR_BGSHIFT;

  bool apply_shadow = true;
  if (term.rvideo) {
    if (fgi >= 256)
      fgi ^= 2;     // (BOLD_)?FG_COLOUR_I <-> (BOLD_)?BG_COLOUR_I
    if (bgi >= 256)
      bgi ^= 2;     // (BOLD_)?FG_COLOUR_I <-> (BOLD_)?BG_COLOUR_I
  }
  if (attr.attr & ATTR_BOLD && cfg.bold_as_colour) {
    if (fgi < 8) {
      if (!cfg.bold_as_font)
        apply_shadow = false;
#ifdef debug_bold
      printf("fgi < 8 (%d %d): apply_shadow %d\n", (int)colours[fgi], (int)colours[fgi | 8], apply_shadow);
#endif
#ifdef enforce_bold
      if (colours[fgi] == colours[fgi | 8])
        apply_shadow = true;
#endif
      fgi |= 8;     // (BLACK|...|WHITE)_I -> BOLD_(BLACK|...|WHITE)_I
    }
    else if (fgi >= 256 && fgi != TRUE_COLOUR && !cfg.bold_as_font) {
      apply_shadow = false;
#ifdef enforce_bold
      if (colours[fgi] == colours[fgi | 1])
        apply_shadow = true;
#endif
      fgi |= 1;     // (FG|BG)_COLOUR_I -> BOLD_(FG|BG)_COLOUR_I
    }
  }
  if (attr.attr & ATTR_BLINK) {
    if (bgi < 8)
      bgi |= 8;
    else if (bgi >= 256)
      bgi |= 1;
  }

  colour fg = fgi >= TRUE_COLOUR ? attr.truefg : colours[fgi];
  colour bg = bgi >= TRUE_COLOUR ? attr.truebg : colours[bgi];

  if (attr.attr & ATTR_DIM) {
    fg = (fg & 0xFEFEFEFE) >> 1; // Halve the brightness.
    if (!cfg.bold_as_colour || fgi >= 256)
      fg += (bg & 0xFEFEFEFE) >> 1; // Blend with background.
  }
  if (attr.attr & ATTR_REVERSE) {
    colour t = fg; fg = bg; bg = t;
  }
  if (attr.attr & ATTR_INVISIBLE)
    fg = bg;

  bool has_cursor = attr.attr & (TATTR_ACTCURS | TATTR_PASCURS);
  colour cursor_colour = 0;

  if (attr.attr & TATTR_CURRESULT) {
    bg = cfg.search_current_colour;
    fg = cfg.search_fg_colour;
  }
  else if (attr.attr & TATTR_RESULT) {
    bg = cfg.search_bg_colour;
    fg = cfg.search_fg_colour;
  }

  if (has_cursor) {
    cursor_colour = colours[ime_open ? IME_CURSOR_COLOUR_I : CURSOR_COLOUR_I];

    //uint mindist = 32768;
    uint mindist = 22222;
    //uint mindist = 8000;
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
    }
  }

  SelectObject(dc, fonts[nfont]);
  SetTextColor(dc, fg);
  SetBkColor(dc, bg);

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
  if (combining_double)
    combining = false;

  int width = char_width * (combining ? 1 : len);
  RECT box = {
    .top = y, .bottom = y + cell_height,
    .left = x, .right = min(x + width, cell_width * term.cols + PADDING)
  };
  RECT box2 = box;
  if (combining_double) {
    box2.left -= char_width;
  }

 /* Array with offsets between neighbouring characters */
  int dxs[len];
  int dx = combining ? 0 : char_width;
  for (int i = 0; i < len; i++)
    dxs[i] = dx;

  bool let_windows_combine = false;
  if (combining) {
   /* Substitute combining characters by overprinting lookalike glyphs */
    for (int i = 0; i < len; i++)
      text[i] = combsubst(text[i]);
   /* Determine characters that should be combined by Windows */
    if (len == 2)
      if (text[0] == 'i' && (text[1] == 0x030F || text[1] == 0x0311))
        let_windows_combine = true;
  }

  int yt = y + (row_spacing / 2) - (lattr == LATTR_BOT ? cell_height : 0);
  int xt = x + (cfg.col_spacing / 2);

  int graph = (attr.attr >> ATTR_GRAPH_SHIFT) & 0xFF;
  if (graph) {
    for (int i = 0; i < len; i++)
      text[i] = ' ';
  }

 /* Determine shadow/overstrike bold or double-width/height width */
  int xwidth = 1;
  if (apply_shadow && bold_mode == BOLD_SHADOW && (attr.attr & ATTR_BOLD)) {
    // This could be scaled with font size, but at risk of clipping
    xwidth = 2;
    if (lattr != LATTR_NORM) {
      xwidth = 3; // 4?
    }
  }

 /* Finally, draw the text */
  SetBkMode(dc, OPAQUE);
  uint overwropt = ETO_OPAQUE;
  trace_line(" TextOut:", text, len);
  // The combining characters separate rendering trick *alone* 
  // makes some combining characters better (~#553, #295), 
  // others worse (#565); however, together with the 
  // substitute combining characters trick it seems to be the best 
  // workaround for combining characters rendering issues.
  // Yet disabling it for some (heuristically determined) cases:
  if (let_windows_combine)
    combining = false;  // disable separate combining characters display
  for (int xoff = 0; xoff < xwidth; xoff++)
    if (combining || combining_double) {
      // Workaround for mangled display of combining characters;
      // Arabic shaping should not be affected as the transformed 
      // presentation forms are not combining characters anymore at this point.
      // Repeat the workaround for bold/wide below.

      // base character
      ExtTextOutW(dc, xt + xoff, yt, eto_options | overwropt, &box, text, 1, dxs);
      if (overwropt) {
        SetBkMode(dc, TRANSPARENT);
        overwropt = 0;
      }
      // combining characters
      for (int i = 1; i < len; i++) {
        int xx = xt + xoff;
        if (combining_double && combiningdouble(text[i]))
          xx -= char_width / 2;
        ExtTextOutW(dc, xx, yt, eto_options, &box2, &text[i], 1, dxs);
      }
    }
    else {
      ExtTextOutW(dc, xt + xoff, yt, eto_options | overwropt, &box, text, len, dxs);
      if (overwropt) {
        SetBkMode(dc, TRANSPARENT);
        overwropt = 0;
      }
    }

  int line_width = (3
                    + (attr.attr & ATTR_BOLD ? 2 : 0)
                    + (lattr >= LATTR_WIDE ? 2 : 0)
                    + (lattr >= LATTR_TOP ? 2 : 0)
                   ) * cell_height / 40;
  if (line_width < 1)
    line_width = 1;

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

  if (graph >> 4) {  // VT100 horizontal lines ⎺⎻(─)⎼⎽
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
  int uloff = descent + (cell_height - descent + 1) / 2;
  if (lattr == LATTR_BOT)
    uloff = descent + (cell_height - descent + 1) / 2;
  uloff += line_width / 2;
  if (uloff >= cell_height)
    uloff = cell_height - 1;

#define dont_debug_underline
#ifdef debug_underline
  ul = 0x802020E0;
  if (lattr == LATTR_TOP)
    ul = 0x80E0E020;
  if (lattr == LATTR_BOT)
    ul = 0x80E02020;
#endif

 /* Underline */
  if (lattr != LATTR_TOP &&
      (force_manual_underline ||
       (und_mode == UND_LINE && (attr.attr & ATTR_UNDER)) ||
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

  if (has_cursor) {
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
        y = yt + min(descent, cell_height - 2);
        y += row_spacing * 3 / 8;
        if (lattr >= LATTR_TOP) {
          y += row_spacing / 2;
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
  HDC dc = GetDC(wnd);
  bool bold = (bold_mode == BOLD_FONT) && (term.curs.attr.attr & ATTR_BOLD);
  bool italic = term.curs.attr.attr & ATTR_ITALIC;
  SelectObject(dc, fonts[(bold ? FONT_BOLD : FONT_NORMAL) | italic ? FONT_ITALIC : 0]);
  ushort glyphs[num];
  GetGlyphIndicesW(dc, wcs, num, glyphs, true);
  for (size_t i = 0; i < num; i++) {
    if (glyphs[i] == 0xFFFF || glyphs[i] == 0x1F)
      wcs[i] = 0;
  }
  ReleaseDC(wnd, dc);
}

#define dont_debug_win_char_width

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
 /* If the font max width is the same as the font average width
  * then this function is a no-op.
  */
#ifndef debug_win_char_width
  if (!font_dualwidth)
    return 1;
#endif

 /* Speedup, I know of no font where ASCII is the wrong width */
#ifdef debug_win_char_width
  if (c != 'A')
#endif
  if (c >= ' ' && c < '~')  // exclude 0x7E (overline in Shift_JIS X 0213)
    return 1;

  HDC dc = GetDC(wnd);
#ifdef debug_win_char_width
  bool ok0 = !!
#endif
  SelectObject(dc, fonts[FONT_NORMAL]);
#ifdef debug_win_char_width
  if (c == 0x2001)
    win_char_width(0x5555);
  if (!ok0)
    printf("width %04X failed (dc %p)\n", c, dc);
  else if (c >= '~' || c == 'A') {
    int cw = 0;
    BOOL ok1 = GetCharWidth32W(dc, c, c, &cw);  // "not on TrueType"
    float cwf = 0.0;
    BOOL ok2 = GetCharWidthFloatW(dc, c, c, &cwf);
    ABC abc; memset(&abc, 0, sizeof abc);
    BOOL ok3 = GetCharABCWidthsW(dc, c, c, &abc);  // only on TrueType
    ABCFLOAT abcf; memset(&abcf, 0, sizeof abcf);
    BOOL ok4 = GetCharABCWidthsFloatW(dc, c, c, &abcf);
    printf("w %04X [%d] (32 %d) %d (32a %d) %d (flt %d) %.3f (abc %d) %2d %2d %2d (abcf %d) %4.1f %4.1f %4.1f\n", 
           c, cell_width, ok1, cw, ok2, cwf, 
           ok3, abc.abcA, abc.abcB, abc.abcC, 
           ok4, abcf.abcfA, abcf.abcfB, abcf.abcfC);
  }
#endif

  int ibuf = 0;
  bool ok = GetCharWidth32W(dc, c, c, &ibuf);
  ReleaseDC(wnd, dc);
  if (!ok)
    return 0;

  // report char as wide if its width is more than 1½ cells
  ibuf += cell_width / 2 - 1;
  ibuf /= cell_width;

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

void
win_set_colour(colour_i i, colour c)
{
  if (i >= COLOUR_NUM)
    return;
  if (c == (colour)-1) {
    // ... reset to default ...
    if (i == BOLD_FG_COLOUR_I) {
      bold_colour_selected = false;
      if (cfg.bold_colour != (colour)-1)
        colours[BOLD_FG_COLOUR_I] = cfg.bold_colour;
      else
        colours[BOLD_FG_COLOUR_I] = brighten(colours[FG_COLOUR_I], colours[BG_COLOUR_I]);
    }
    return;
  }
  colours[i] = c;
#ifdef debug_brighten
  printf ("colours[%d] = %06X\n", i, c);
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

colour win_get_colour(colour_i i)
{
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

  // Colour cube
  colour_i i = 16;
  for (uint r = 0; r < 6; r++)
    for (uint g = 0; g < 6; g++)
      for (uint b = 0; b < 6; b++)
        colours[i++] = RGB (r ? r * 40 + 55 : 0,
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
}
