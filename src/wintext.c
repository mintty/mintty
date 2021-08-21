// wintext.c (part of mintty)
// Copyright 2008-13 Andy Koppe, 2015-2018 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "winsearch.h"
#include "charset.h"  // wcscpy, wcsncat, combiningdouble
#include "config.h"
#include "winimg.h"  // winimgs_paint
#include "tek.h"

#include <winnls.h>
#include <usp10.h>  // Uniscribe


#define dont_debug_bold 1

#define dont_narrow_via_font

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
  FONT_ZOOMSMALL = 0x40,
  FONT_ZOOMDOWN  = 0x80,
  FONT_WIDE      = 0x100,
#ifdef narrow_via_font
#warning narrowing via font is deprecated
  FONT_NARROW    = 0x200,
  FONT_MAXNO     = FONT_WIDE + FONT_NARROW
#else
  FONT_NARROW    = 0,	// disabled narrowing via font
  FONT_MAXNO     = 2 * FONT_WIDE
#endif
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


// colour values; this should perhaps be part of struct term
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
int OFFSET = 0;
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

int line_scale;

wchar
win_linedraw_char(int i)
{
  int findex = (term.curs.attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  if (findex > 10)
    findex = 0;
  struct fontfam * ff = &fontfamilies[findex];
  return ff->win_linedraw_chars[i];
}


char *
fontpropinfo()
{
  //__ Options - Text: font properties information: "Leading": total line padding (see option RowSpacing), Bold/Underline modes (font or manual, see options BoldAsFont/UnderlineManual/UnderlineColour)
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
brighten(colour c, colour against, bool monotone)
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
      if (monotone) {
        uint r = red(bright), g = green(bright), b = blue(bright);
        return make_colour(r - (r >> 2), g - (g >> 2), b - (b >> 2));
      }
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
      if (monotone) {
        uint r = red(bright), g = green(bright), b = blue(bright);
        return make_colour(r + ((256 - r) >> 2), g + ((256 - g) >> 2), b + ((256 - b) >> 2));
      }
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

#define dont_debug_fonts 1

#define dont_debug_win_char_width_init

#if defined(debug_fonts) && debug_fonts > 0
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

static char * font_warnings = 0;

static void
font_warning(struct fontfam * ff, char * msg)
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
  if (font_warnings) {
    char * newfw = asform("%s\n%s:\n%s", font_warnings, msg, fn);
    free(font_warnings);
    font_warnings = newfw;
  }
  else
    font_warnings = asform("%s:\n%s", msg, fn);
  free(fn);
}

static void
show_font_warnings(void)
{
  if (font_warnings) {
    show_message(font_warnings, MB_ICONWARNING);
    free(font_warnings);
    font_warnings = 0;
  }
}


#ifndef TCI_SRCLOCALE
//old MinGW
#define TCI_SRCLOCALE 0x1000
#endif

#ifdef check_font_ranges
#warning this does not tell us whether a glyph is shown

static GLYPHSET *
win_font_ranges(HDC dc, struct fontfam * ff, int fontno)
{
  if (!ff->fonts[fontno] || fontno >= FONT_BOLDITAL)
    return 0;
  SelectObject(dc, ff->fonts[fontno]);
  int ursize = GetFontUnicodeRanges(dc, 0);
  GLYPHSET * gs = malloc(ursize);
  gs->cbThis = ursize;
  gs->flAccel = 0;
  if (GetFontUnicodeRanges(dc, gs)) {
#ifdef debug_font_ranges
    printf("%d %ls\n", fontno, ff->name);
    for (uint i = 0; i < gs->cRanges; i++) {
      printf("%04X: %d\n", gs->ranges[i].wcLow, gs->ranges[i].cGlyphs);
    }
#endif
  }
  return gs;
}

static bool
glyph_in(WCHAR c, GLYPHSET * gs)
{
  int min = 0;
  int max = gs->cRanges - 1;
  int mid;
  while (max >= min) {
    mid = (min + max) / 2;
    if (c < gs->ranges[mid].wcLow) {
      max = mid - 1;
    } else if (c < gs->ranges[mid].wcLow + gs->ranges[mid].cGlyphs) {
      return true;
    } else {
      min = mid + 1;
    }
  }
  return false;
}

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

struct data_adjust_font_weights {
  struct fontfam *ff;
  int fw_norm_0, fw_bold_0, fw_norm_1, fw_bold_1, default_charset;
  bool font_found, ansi_found, cs_found;
};

static int CALLBACK
enum_fonts_adjust_font_weights(const LOGFONTW * lfp, const TEXTMETRICW * tmp, DWORD fontType, LPARAM lParam)
{
  struct data_adjust_font_weights *data = (struct data_adjust_font_weights *)lParam;
  (void)tmp;
  (void)fontType;

#if defined(debug_fonts) && debug_fonts > 1
  trace_font(("%ls %dx%d %d it %d cs %d %s\n", lfp->lfFaceName, (int)lfp->lfWidth, (int)lfp->lfHeight, (int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));
#endif

  data->font_found = true;
  if (lfp->lfCharSet == ANSI_CHARSET)
    data->ansi_found = true;
  if (lfp->lfCharSet == data->default_charset || lfp->lfCharSet == DEFAULT_CHARSET)
    data->cs_found = true;

  if (lfp->lfWeight > data->fw_norm_0 && lfp->lfWeight <= data->ff->fw_norm)
    data->fw_norm_0 = lfp->lfWeight;
  if (lfp->lfWeight > data->fw_bold_0 && lfp->lfWeight <= data->ff->fw_bold)
    data->fw_bold_0 = lfp->lfWeight;
  if (lfp->lfWeight < data->fw_norm_1 && lfp->lfWeight >= data->ff->fw_norm)
    data->fw_norm_1 = lfp->lfWeight;
  if (lfp->lfWeight < data->fw_bold_1 && lfp->lfWeight >= data->ff->fw_bold)
    data->fw_bold_1 = lfp->lfWeight;

  return 1;  // continue
}

static void
adjust_font_weights(struct fontfam * ff, int findex)
{
  LOGFONTW lf;
  wcscpy(lf.lfFaceName, W(""));
  wcsncat(lf.lfFaceName, ff->name, lengthof(lf.lfFaceName) - 1);
  lf.lfPitchAndFamily = 0;
  //lf.lfCharSet = ANSI_CHARSET;   // report only ANSI character range
  // use this to avoid double error popup (e.g. Font=David):
  lf.lfCharSet = DEFAULT_CHARSET;  // report all supported char ranges

  // find the closest available widths such that
  // fw_norm_0 <= ff->fw_norm <= fw_norm_1
  // fw_bold_0 <= ff->fw_bold <= fw_bold_1
  int default_charset = get_default_charset();
  struct data_adjust_font_weights data = {
    .ff = ff,
    .fw_norm_0 = 0,
    .fw_bold_0 = 0,
    .fw_norm_1 = 1000,
    .fw_bold_1 = 1001,
    .default_charset = default_charset,
    .font_found = false,
    .ansi_found = false,
    .cs_found = default_charset == DEFAULT_CHARSET
  };

  // do not enumerate all fonts for unspecified alternative font
  if (ff->name[0] == 0) {
    ff->fw_norm = 400;
    ff->fw_bold = 700;
    trace_font(("--\n"));
    return;
  }

  HDC dc = GetDC(0);
  EnumFontFamiliesExW(dc, &lf, enum_fonts_adjust_font_weights, (LPARAM)&data, 0);
  trace_font(("font width (%d)%d(%d)/(%d)%d(%d)", data.fw_norm_0, ff->fw_norm, data.fw_norm_1, data.fw_bold_0, ff->fw_bold, data.fw_bold_1));
  ReleaseDC(0, dc);

  // check if no font found
  if (!data.font_found) {
    font_warning(ff, _("Font not found, using system substitute"));
    ff->fw_norm = 400;
    ff->fw_bold = 700;
    trace_font(("//\n"));
    return;
  }
  if (!data.ansi_found && !data.cs_found) {
    string l;
    if (!strcmp(cfg.charset, "CP437") || ((l = getlocenvcat("LC_CTYPE")) && strstr(l, "CP437"))) {
      // accept limited range
    }
    else if (findex) {
      // don't report for alternative / secondary fonts
    }
    else
      font_warning(ff, _("Font has limited support for character ranges"));
  }

  // find available widths closest to selected widths
  if (abs(ff->fw_norm - data.fw_norm_0) <= abs(ff->fw_norm - data.fw_norm_1) && data.fw_norm_0 > 0)
    ff->fw_norm = data.fw_norm_0;
  else if (data.fw_norm_1 < 1000)
    ff->fw_norm = data.fw_norm_1;
  if (abs(ff->fw_bold - data.fw_bold_0) < abs(ff->fw_bold - data.fw_bold_1) || data.fw_bold_1 > 1000)
    ff->fw_bold = data.fw_bold_0;
  else if (data.fw_bold_1 < 1001)
    ff->fw_bold = data.fw_bold_1;
  // ensure bold is bolder than normal
  if (ff->fw_bold <= ff->fw_norm) {
    trace_font((" -> %d/%d", ff->fw_norm, ff->fw_bold));
    if (data.fw_norm_0 < ff->fw_norm && data.fw_norm_0 > 0)
      ff->fw_norm = data.fw_norm_0;
    if (ff->fw_bold - ff->fw_norm < 300) {
      if (data.fw_bold_1 > ff->fw_bold && data.fw_bold_1 < 1001)
        ff->fw_bold = data.fw_bold_1;
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
    ff->fontflag[i] = false;
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
    adjust_font_weights(ff, findex);
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
  trace_font(("created font %s %d it %d cs %d\n", logfont.lfFaceName, (int)logfont.lfWeight, logfont.lfItalic, logfont.lfCharSet));
  SelectObject(dc, ff->fonts[FONT_NORMAL]);
  GetTextMetrics(dc, &tm);
  if (!tm.tmHeight) {
    // corrupt font installation (e.g. deleted font file)
    font_warning(ff, _("Font installation corrupt, using system substitute"));
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
    font_warning(ff, _("Font does not support system locale"));
  }
#endif

  float latin_char_width, greek_char_width, line_char_width, cjk_char_width;
  GetCharWidthFloatW(dc, 0x0041, 0x0041, &latin_char_width);
  GetCharWidthFloatW(dc, 0x03B1, 0x03B1, &greek_char_width);
  GetCharWidthFloatW(dc, 0x2500, 0x2500, &line_char_width);
  GetCharWidthFloatW(dc, 0x4E00, 0x4E00, &cjk_char_width);

  // avoid trouble with non-text font (#777, Noto Sans Symbols2)
  if (!latin_char_width) {
    //GetCharWidthFloatW(dc, 0x0020, 0x0020, &latin_char_width);
    latin_char_width = (float)font_size / 16;
  }

  if (!findex) {
    ff->row_spacing = 0;
    if (cfg.auto_leading == 1) {
      //?int ilead = tm.tmInternalLeading - (dpi - 96) / 48;
      int idpi = dpi;  // avoid coercion of tm.tmInternalLeading to unsigned
      int ilead = tm.tmInternalLeading * 96 / idpi;
      ff->row_spacing = row_padding(ilead, tm.tmExternalLeading);
      //printf("row_sp dpi %d int %d -> ild %d (ext %d) -> pad %d + cfg %d\n", dpi, (int)tm.tmInternalLeading, ilead, (int)tm.tmExternalLeading, ff->row_spacing, cfg.row_spacing);
      trace_font(("00 height %d avwidth %d asc %d dsc %d intlead %d extlead %d %ls\n", 
                 (int)tm.tmHeight, (int)tm.tmAveCharWidth, (int)tm.tmAscent, (int)tm.tmDescent, 
                 (int)tm.tmInternalLeading, (int)tm.tmExternalLeading, 
                 ff->name));
    }
    else if (cfg.auto_leading == 2) {
      /*
      	–	tmIntLeading	|	|
      	M			|ascent	| tmHeight
      	Mg			|	|
      	 g	tmDescent		|
      	–	tmExtLeading
      */
      if (tm.tmInternalLeading < 2)
        ff->row_spacing += 2 - tm.tmInternalLeading;
      else if (tm.tmInternalLeading > 7)
        ff->row_spacing -= tm.tmExternalLeading;
      trace_font(("vert geom: (int %d) asc %d + dsc %d -> hei %d, + ext %d; -> spc (%d) %d %ls\n", 
                (int)tm.tmInternalLeading, (int)tm.tmAscent, (int)tm.tmDescent,
                (int)tm.tmHeight, (int)tm.tmExternalLeading, 
                row_spacing_1, ff->row_spacing,
                ff->name));
    }

    ff->row_spacing += cfg.row_spacing;
    if (ff->row_spacing < -tm.tmDescent)
      ff->row_spacing = -tm.tmDescent;
    //printf("row_sp %d\n", ff->row_spacing);
    trace_font(("row spacing int %d ext %d -> %+d; add %+d -> %+d; desc %d -> %+d %ls\n", 
        (int)tm.tmInternalLeading, (int)tm.tmExternalLeading, row_padding(tm.tmInternalLeading, tm.tmExternalLeading),
        cfg.row_spacing, row_padding(tm.tmInternalLeading, tm.tmExternalLeading) + cfg.row_spacing,
        (int)tm.tmDescent, ff->row_spacing, ff->name));
    ff->col_spacing = cfg.col_spacing;

    cell_height = tm.tmHeight + ff->row_spacing;
    //cell_width = tm.tmAveCharWidth + ff->col_spacing;
    cell_width = (int)(latin_char_width * 16) + ff->col_spacing;

    line_scale = cell_height * 100 / abs(font_height);

    PADDING = tm.tmAveCharWidth;
    if (cfg.padding >= 0 && cfg.padding < PADDING)
      PADDING = cfg.padding;
  }
  else {
    ff->row_spacing = cell_height - tm.tmHeight;
    //ff->col_spacing = cell_width - tm.tmAveCharWidth;
    ff->col_spacing = cell_width - (int)(latin_char_width * 16);
  }

#ifdef debug_create_font
  printf("size %d -> height %d -> height %d\n", font_size, font_height, cell_height);
#endif

  //ff->font_dualwidth = (tm.tmMaxCharWidth >= tm.tmAveCharWidth * 3 / 2);
  ff->font_dualwidth = (cjk_char_width >= latin_char_width * 3 / 2);

  // Determine whether ambiguous-width characters are wide in this font */
  if (!findex)
    font_ambig_wide =
      greek_char_width >= latin_char_width * 1.5 ||
      line_char_width  >= latin_char_width * 1.5;

#ifdef debug_win_char_width_init
  int w_latin = win_char_width(0x0041, 0);
  int w_greek = win_char_width(0x03B1, 0);
  int w_lines = win_char_width(0x2500, 0);
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

  int fontsize[FONT_UNDERLINE + 1];
#ifdef handle_baseline_leap
  int base_ascent = tm.tmAscent;
#endif
  for (uint i = 0; i < lengthof(fontsize); i++) { // could skip FONT_ITALIC here
    if (ff->fonts[i]) {
      if (SelectObject(dc, ff->fonts[i]) && GetTextMetrics(dc, &tm)) {
        fontsize[i] = tm.tmAveCharWidth + 256 * tm.tmHeight;
        trace_font(("%02X height %d avwidth %d asc %d dsc %d intlead %d extlead %d %ls\n", 
                    i, (int)tm.tmHeight, (int)tm.tmAveCharWidth, 
                    (int)tm.tmAscent, (int)tm.tmDescent, 
                    (int)tm.tmInternalLeading, (int)tm.tmExternalLeading, 
                    ff->name));
#ifdef handle_baseline_leap
        if (i == FONT_BOLD && tm.tmAscent < base_ascent) {
          // for Courier New, this correlates with a significant visual leap 
          // of the bold font from the baseline of the normal font,
          // but not for other fonts; so let's do nothing
        }
#endif
      }
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

  if (ff->bold_mode == BOLD_FONT) {
    int diffsize = abs(fontsize[FONT_BOLD] - fontsize[FONT_NORMAL]);
#if defined(debug_create_font) || defined(debug_bold)
    if (*ff->name)
      printf("bold_mode %d font_size %d size %d bold %d diff %d %s %ls\n",
             ff->bold_mode, font_size,
             fontsize[FONT_NORMAL], fontsize[FONT_BOLD], diffsize,
             fontsize[FONT_BOLD] != fontsize[FONT_NORMAL] ? "=/=" : "===",
             ff->name);
#endif
    if (diffsize * 16 > fontsize[FONT_NORMAL]) {
      trace_font(("bold_mode %d\n", ff->bold_mode));
      ff->bold_mode = BOLD_SHADOW;
      DeleteObject(ff->fonts[FONT_BOLD]);
      ff->fonts[FONT_BOLD] = 0;
    }
  }

  trace_font(("bold_mode %d\n", ff->bold_mode));
  ff->fontflag[FONT_NORMAL] = true;
  ff->fontflag[FONT_BOLD] = true;
  ff->fontflag[FONT_UNDERLINE] = true;
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

static int CALLBACK
enum_fonts(const LOGFONTW * lfp, const TEXTMETRICW * tmp, DWORD fontType, LPARAM lParam)
{
  (void)tmp;
  (void)fontType;
  wstring * fnp = (wstring *)lParam;

#if defined(debug_fonts) && debug_fonts > 1
  trace_font(("%ls %dx%d %d it %d cs %d %s\n", lfp->lfFaceName, (int)lfp->lfWidth, (int)lfp->lfHeight, (int)lfp->lfWeight, lfp->lfItalic, lfp->lfCharSet, (lfp->lfPitchAndFamily & 3) == FIXED_PITCH ? "fixed" : ""));
#endif
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

void
findFraktur(wstring * fnp)
{
  LOGFONTW lf;
  wcscpy(lf.lfFaceName, W(""));
  lf.lfPitchAndFamily = 0;
  lf.lfCharSet = ANSI_CHARSET;   // report only ANSI character range

  HDC dc = GetDC(0);
  EnumFontFamiliesExW(dc, 0, enum_fonts, (LPARAM)fnp, 0);
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
  if (initinit)
    show_font_warnings();
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
static bool ime_open = false;

static int update_skipped = 0;
int lines_scrolled = 0;

#define dont_debug_cursor 1

static struct charnameentry {
  xchar uc;
  string un;
} * charnametable = null;
static int charnametable_len = 0;
static int charnametable_alloced = 0;
static bool charnametable_init = false;

static void
init_charnametable()
{
  if (charnametable_init)
    return;
  charnametable_init = true;

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
  FILE * cnf = 0;
  if (cnfn) {
    cnf = fopen(cnfn, "r");
    free(cnfn);
  }
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
      if (nonascii(cs)) {
        wchar * wcs = cs__utftowcs(cs);
        SetWindowTextW(wnd, wcs);
        free(wcs);
      }
      else
        SetWindowTextA(wnd, cs);
    }
    if (prev)
      free(prev);
    prev = cs;
  }

  void show_char_info(termchar * cpoi) {
    char * cs = 0;

    // return if base character same as previous and no combining chars
    if (cpoi == pp && cpoi && cpoi->chr == prev.chr && !cpoi->cc_next)
      return;

#define dont_debug_emojis

    if (cfg.emojis && (cpoi->attr.attr & TATTR_EMOJI)) {
      if (cpoi == pp)
        return;
      cs = get_emoji_description(cpoi);
#ifdef debug_emojis
      printf("Emoji sequence: %s\n", cs);
#endif
    }

    pp = cpoi;

    if (!cs && cpoi) {
      prev = *cpoi;

      cs = strdup("");

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


#define update_timer 16

void
do_update(void)
{
  //if (kb_trace) printf("[%ld] do_update\n", mtime());

#if defined(debug_cursor) && debug_cursor > 1
  printf("do_update cursor_on %d @%d,%d\n", term.cursor_on, term.curs.y, term.curs.x);
#endif
  //printf("do_update state %d susp %d\n", update_state, term.suspend_update);
  if (update_state == UPDATE_BLOCKED) {
    update_state = UPDATE_IDLE;
    return;
  }

  update_skipped++;
  int output_speed = lines_scrolled / (term.rows ?: cfg.rows);
  lines_scrolled = 0;
  if ((update_skipped < cfg.display_speedup && cfg.display_speedup < 10
       && output_speed > update_skipped
       //&& !term.smooth_scroll ?
      ) || (!term.detect_progress && win_is_iconic())
        //|| win_is_hidden() ?
        // suspend display update:
        //|| (update_skipped < term.suspend_update * cfg.display_speedup)
        || (update_skipped * update_timer < term.suspend_update)
     )
  {
    //printf("skip %d susp %d\n", update_skipped, term.suspend_update);
    win_set_timer(do_update, update_timer);
    return;
  }
  update_skipped = 0;
  term.suspend_update = 0;

  update_state = UPDATE_BLOCKED;

  show_curchar_info('u');

  dc = GetDC(wnd);

  win_paint_exclude_search(dc);
  term_update_search();

  if (tek_mode)
    tek_paint();
  else {
    term_paint();
    winimgs_paint();
  }

  ReleaseDC(wnd, dc);

  // Update scrollbar
  if (cfg.scrollbar && term.show_scrollbar && !term.app_scrollbar) {
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
    int y = (term.curs.y - term.disptop) * cell_height + OFFSET + PADDING;
    SetCaretPos(x, y);
    if (ime_open) {
      COMPOSITIONFORM cf = {.dwStyle = CFS_POINT, .ptCurrentPos = {x, y}};
      ImmSetCompositionWindow(imc, &cf);
    }
  }

  // Schedule next update.
  win_set_timer(do_update, update_timer);
}

#include <math.h>

/*
   Indicate size of selection with a popup tip (option SelectionShowSize).
   Future enhancements may be automatic position flipping depending 
   on selection direction or if the tip reaches outside the screen.
   Also the actual tip window should better be decoupled from the 
   window size tip which is now abused for this feature.
 */
static void
sel_update(bool update_sel_tip)
{
  static bool selection_tip_active = false;
  //printf("sel_update tok %d sel %d act %d\n", tip_token, term.selected, selection_tip_active);
  if (term.selected && cfg.selection_show_size && update_sel_tip) {
    int cols, rows;
    if (term.sel_rect) {
      rows = abs(term.sel_end.y - term.sel_start.y) + 1;
      cols = abs(term.sel_end.x - term.sel_start.x);
    }
    else {
      rows = term.sel_end.y - term.sel_start.y + 1;
      if (rows == 1)
        cols = term.sel_end.x - term.sel_start.x;
      else
        cols = term.cols;
    }
    RECT wr;
    GetWindowRect(wnd, &wr);
    LONG style = GetWindowLong(wnd, GWL_STYLE);
    int x = wr.left
          + ((style & WS_THICKFRAME) ? GetSystemMetrics(SM_CXSIZEFRAME) : 0)
          + PADDING + last_pos.x * cell_width;
    int y = wr.top
          + ((style & WS_THICKFRAME) ? GetSystemMetrics(SM_CYSIZEFRAME) : 0)
          + ((style & WS_CAPTION) ? GetSystemMetrics(SM_CYCAPTION) : 0)
          + OFFSET + PADDING + last_pos.y * cell_height;
#ifdef debug_selection_show_size 
    cfg.selection_show_size = cfg.selection_show_size % 12 + 1;
#endif
    int w = 30, h = 18;  // assumed size of tip window
    float phi = 2 * 3.1415 / 12 * (cfg.selection_show_size + 9);
    float rx = cell_width * 1.5;
    float ry = cell_height * 1.5;
    int dx = cell_width / 2 + rx * cos(phi) - w / 2;
    int dy = cell_height / 2 + ry * sin(phi) - h / 2;
    //printf("selection_show_size [%d]: %.2f %.2f %.2f\n", cfg.selection_show_size, phi, cos(phi), sin(phi));
    win_show_tip(x + dx, y + dy, cols, rows);
    selection_tip_active = true;
  }
  else if (!term.selected && selection_tip_active) {
    win_destroy_tip();
    selection_tip_active = false;
  }
}

static void
show_link(void)
{
  static int lasthoverlink = -1;

  int hoverlink = term.hovering ? term.hoverlink : -1;
  if (hoverlink != lasthoverlink) {
    lasthoverlink = hoverlink;

    char * url = geturl(hoverlink) ?: "";

    if (nonascii(url)) {
      wchar * wcs = cs__utftowcs(url);
      SetWindowTextW(wnd, wcs);
      free(wcs);
    }
    else
      SetWindowTextA(wnd, url);
  }
}

void
win_update_now(void)
{
  if (update_state == UPDATE_PENDING)
    update_state = UPDATE_IDLE;
  win_update(false);
}

void
win_update(bool update_sel_tip)
{
  //if (kb_trace) printf("[%ld] win_update state %d (idl/blk/pnd)\n", mtime(), update_state);
  trace_resize(("----- win_update\n"));

  if (update_state == UPDATE_IDLE)
    do_update();
  else
    update_state = UPDATE_PENDING;

  sel_update(update_sel_tip);
  if (cfg.hover_title)
    show_link();
}

void
win_schedule_update(void)
{
  //if (kb_trace) printf("[%ld] win_schedule_update state %d (idl/blk/pnd)\n", mtime(), update_state);

  if (update_state == UPDATE_IDLE)
    win_set_timer(do_update, update_timer);
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
  if (fontno & FONT_ZOOMSMALL) {
    y = y * 12 / 20;
    x = x * 12 / 20;
  }
  if (fontno & FONT_ZOOMDOWN) {
    y = y / 2;
    x = x / 2;
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

  ff->fontflag[fontno] = true;
}

void
win_set_ime_open(bool open)
{
  if (open != ime_open) {
    ime_open = open;
    term.cursor_invalid = true;
    win_update(false);
  }
}


/*
   Background texture/image.
 */

static bool tiled = false;
static bool ratio = false;
static bool wallp = false;
static int wallp_style;
static int alpha = -1;
static LONG w = 0, h = 0;
static HBRUSH bgbrush_bmp = 0;

static BOOL (WINAPI *pAlphaBlend)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION) = 0;

static HBITMAP
alpha_blend_bg(int alpha, HDC dc, HBITMAP hbm, int bw, int bh, colour bg)
{
  // load GDI function
  if (!pAlphaBlend) {
    pAlphaBlend = load_library_func("msimg32.dll", "AlphaBlend");
  }
  if (!pAlphaBlend)
    return hbm;

  // take size from hbm if not passed explicitly
  if (!bw || !bh) {
    BITMAP bm0;
    if (!GetObject(hbm, sizeof(BITMAP), &bm0))
      return hbm;
    bw = bm0.bmWidth;
    bh = bm0.bmHeight;
  }

  // prepare source memory DC and select the source bitmap into it
  HDC dc0 = CreateCompatibleDC(dc);
  HBITMAP oldhbm0 = SelectObject(dc0, hbm);

  // prepare destination memory DC, 
  // create and select the destination bitmap into it
  HDC dc1 = CreateCompatibleDC(dc);
  HBITMAP hbm1 = CreateCompatibleBitmap(dc0, bw, bh);
  HBITMAP oldhbm1 = SelectObject(dc1, hbm1);

  HBRUSH bgb = CreateSolidBrush(bg);
  FillRect(dc1, &(RECT){0, 0, bw, bh}, bgb);
  DeleteObject(bgb);

  BYTE alphafmt = alpha == 255 ? AC_SRC_ALPHA : 0;
  BLENDFUNCTION bf = (BLENDFUNCTION) {AC_SRC_OVER, 0, alpha, alphafmt};
  int ok = pAlphaBlend(dc1, 0, 0, bw, bh, dc0, 0, 0, bw, bh, bf);

  // release everything
  SelectObject(dc1, oldhbm1);
  SelectObject(dc0, oldhbm0);
  DeleteDC(dc1);
  DeleteDC(dc0);

  if (ok) {
    DeleteObject(hbm);
    return hbm1;
  }
  else
    return hbm;
}

static void
offset_bg(HDC dc)
{
  RECT wr;
  GetWindowRect(wnd, &wr);
  int wx = wr.left + GetSystemMetrics(SM_CXSIZEFRAME);
  int wy = wr.top + GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION);

  // adjust wallpaper origin

  SetBrushOrgEx(dc, -wx, -wy, 0);
}

#if CYGWIN_VERSION_API_MINOR >= 74

#include <w32api/wtypes.h>
#include <w32api/gdiplus/gdiplus.h>
#include <w32api/gdiplus/gdiplusflat.h>

static GpBrush * bgbrush_img = 0;
static GpGraphics * bg_graphics = 0;

#define dont_debug_gdiplus

#ifdef debug_gdiplus
static void
gpcheck(char * tag, GpStatus s)
{
  static char * gps[] = {
    "Ok",
    "GenericError",
    "InvalidParameter",
    "OutOfMemory",
    "ObjectBusy",
    "InsufficientBuffer",
    "NotImplemented",
    "Win32Error",
    "WrongState",
    "Aborted",
    "FileNotFound",
    "ValueOverflow",
    "AccessDenied",
    "UnknownImageFormat",
    "FontFamilyNotFound",
    "FontStyleNotFound",
    "NotTrueTypeFont",
    "UnsupportedGdiplusVersion",
    "GdiplusNotInitialized",
    "PropertyNotFound",
    "PropertyNotSupported",
    "ProfileNotFound",
  };
  if (s)
    printf("[%s] %d %s\n", tag, s, s >= 0 && s < lengthof(gps) ? gps[s] : "?");
}
#else
#define gpcheck(tag, s)	(void)s
#endif

static void
drop_background_image_brush(void)
{
  if (bgbrush_img) {
    GpStatus s = GdipDeleteBrush(bgbrush_img);
    gpcheck("delete brush", s);
    bgbrush_img = 0;
  }
}

static void
init_gdiplus(void)
{
  static GdiplusStartupInput gi = (GdiplusStartupInput){1, NULL, FALSE, FALSE};
  static ULONG_PTR gis = 0;
  if (!gis) {
    GpStatus s = GdiplusStartup(&gis, &gi, NULL);
    gpcheck("startup", s);
  }
}

static bool
get_image_size(wstring fn, uint * _bw, uint * _bh)
{
  GpStatus s;

  init_gdiplus();

  GpBitmap * gbm = 0;
  s = GdipCreateBitmapFromFile(fn, &gbm);
  gpcheck("bitmap from file", s);
  if (s != Ok || !gbm)
    return false;

  GpStatus stsz = GdipGetImageWidth(gbm, _bw);
  gpcheck("get size", stsz);
  if (stsz == Ok) {
    stsz = GdipGetImageHeight(gbm, _bh);
    gpcheck("get size", stsz);
  }

  s = GdipDisposeImage(gbm);
  gpcheck("dispose bitmap", s);

  if (stsz != Ok) {
    HBITMAP hbm = 0;
    s = GdipCreateHBITMAPFromBitmap(gbm, &hbm, 0);
    gpcheck("convert bitmap", s);

    BITMAP bm0;
    if (!GetObject(hbm, sizeof(BITMAP), &bm0))
      return false;
    *_bw = bm0.bmWidth;
    *_bh = bm0.bmHeight;
    DeleteObject(hbm);
  }
  return true;
}

static void
load_background_image_brush(HDC dc, wstring fn)
{
  GpStatus s;

  init_gdiplus();

  drop_background_image_brush();

  // try to provide a GDI brush from a GDI+ image
  // (because a GDI brush is much more efficient than a GDI+ brush)
  GpBitmap * gbm = 0;
  s = GdipCreateBitmapFromFile(fn, &gbm);
  gpcheck("bitmap from file", s);

  if (s == Ok && gbm) {
    HBITMAP hbm = 0;
    s = GdipCreateHBITMAPFromBitmap(gbm, &hbm, 0);
    gpcheck("convert bitmap", s);

    if (!tiled) {
      // scale the bitmap; 
      // wtf is this a complex task @ braindamaged Windows API
      // https://www.experts-exchange.com/questions/28594399/Whats-the-best-way-to-scale-a-windows-bitmap.html

      uint bw, bh;
      GpStatus stsz = GdipGetImageWidth(gbm, &bw);
      gpcheck("get size", stsz);
      if (stsz == Ok) {
        stsz = GdipGetImageHeight(gbm, &bh);
        gpcheck("get size", stsz);
      }

      s = GdipDisposeImage(gbm);
      gpcheck("dispose bitmap", s);
      gbm = 0;

      if (stsz != Ok) {
        BITMAP bm0;
        if (!GetObject(hbm, sizeof(BITMAP), &bm0))
          return;
        bw = bm0.bmWidth;
        bh = bm0.bmHeight;
      }

#ifdef scale_to_aspect_ratio_asynchronously
      // keep aspect ratio of background image if requested
      static wchar * prevbg = 0;
      bool isnewbg = !prevbg || wcscmp(cfg.background, prevbg);
      printf("isnewbg %d <%ls> <%ls>\n", isnewbg, cfg.background, prevbg);
      if (ratio && isnewbg && abs((int)bw * h - (int)bh * w) > 5) {
        if (prevbg)
          free(prevbg);
        prevbg = wcsdup(cfg.background);

        int xh, xw;
        if (bw * h < bh * w) {
          xh = h;
          xw = bw * xh / bh;
        } else {
          xw = w;
          xh = bh * xw / bw;
        }
        int sy = win_search_visible() ? SEARCHBAR_HEIGHT : 0;
        printf("%dx%d (%dx%d) -> %dx%d\n", (int)h, (int)w, bh, bw, xh, xw);
        // rescale window to aspect ratio of background image
        win_set_pixels(xh - 2 * PADDING - OFFSET - sy, xw - 2 * PADDING);
        // WARNING: rescaling asynchronously at this point makes 
        // terminal geometry (term.rows, term.cols) inconsistent with 
        // running operations and may crash mintty; 
        // postponing the resizing with SendMessage does not help;
        // therefore try to update mintty data now; 
        // this seems to help a bit, but not completely;
        // that's why this embedded approach is disabled
        do_update();
        w = xw;
        h = xh;
      }
#endif

      // prepare source memory DC and select the source bitmap into it
      HDC dc0 = CreateCompatibleDC(dc);
      HBITMAP oldhbm0 = SelectObject(dc0, hbm);

      // prepare destination memory DC, 
      // create and select the destination bitmap into it
      HDC dc1 = CreateCompatibleDC(dc);
      HBITMAP hbm1 = CreateCompatibleBitmap(dc0, w, h);
      HBITMAP oldhbm1 = SelectObject(dc1, hbm1);

      if (alpha >= 0 && !pAlphaBlend) {
        pAlphaBlend = load_library_func("msimg32.dll", "AlphaBlend");
      }

      if (alpha < 0 || !pAlphaBlend) {
        // set half-tone stretch-blit mode for scaling quality
        SetStretchBltMode(dc1, HALFTONE);
        // draw the bitmap scaled into the destination memory DC
        StretchBlt(dc1, 0, OFFSET, w, h - OFFSET, dc0, 0, 0, bw, bh, SRCCOPY);

        DeleteObject(hbm);
        hbm = hbm1;
      }
      else {
#ifdef fill_bg_with_rectangle
#warning missing border HPEN
        HBRUSH oldbrush = SelectObject(dc1, CreateSolidBrush(win_get_colour(BG_COLOUR_I)));
        Rectangle(dc1, 0, 0, w, h);
        DeleteObject(SelectObject(dc1, oldbrush));
#else
        HBRUSH br = CreateSolidBrush(win_get_colour(BG_COLOUR_I));
        FillRect(dc1, &(RECT){0, 0, w, h}, br);
        DeleteObject(br);
#endif

        BYTE alphafmt = alpha == 255 ? AC_SRC_ALPHA : 0;
        BLENDFUNCTION bf = (BLENDFUNCTION) {AC_SRC_OVER, 0, alpha, alphafmt};
        if (pAlphaBlend(dc1, 0, OFFSET, w, h - OFFSET, dc0, 0, 0, bw, bh, bf)) {
          DeleteObject(hbm);
          hbm = hbm1;
        }
      }

      // release everything
      SelectObject(dc1, oldhbm1);
      SelectObject(dc0, oldhbm0);
      DeleteDC(dc1);
      DeleteDC(dc0);
    }
    else {  // tiled
      if (alpha >= 0) {
        uint bw = 0, bh = 0;
        s = GdipGetImageWidth(gbm, &bw);
        gpcheck("get size", s);
        s = GdipGetImageHeight(gbm, &bh);
        gpcheck("get size", s);
        hbm = alpha_blend_bg(alpha, dc, hbm, bw, bh, win_get_colour(BG_COLOUR_I));
      }

      s = GdipDisposeImage(gbm);
      gpcheck("dispose bitmap", s);
      gbm = 0;
    }

    // now we have the (scaled or to-be-tiled) bitmap in 'hbm'
    if (hbm) {
      bgbrush_bmp = CreatePatternBrush(hbm);
      DeleteObject(hbm);
      if (bgbrush_bmp) {
        RECT cr;
        GetClientRect(wnd, &cr);
        // support tabbar
        cr.top += OFFSET;

        /* By applying this tweak here and (!) in fill_background below,
           we can apply an offset to the origin of a wallpaper background, 
           in order to simulate a floating window.
         */
        if (wallp) {
          offset_bg(dc);
        }

        FillRect(dc, &cr, bgbrush_bmp);
        drop_background_image_brush();
        return;
      }
    }
  }

#ifdef use_gdiplus_brush_fallback
  DWORD win_version = GetVersion();
  win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
  if (win_version > 0x0601)  // not Windows 7 or XP
    return;

  // creating a GDI brush failed,
  // try to provide a GDI+ brush (does not work on Windows 10)
  GpImage * img = 0;
  s = GdipLoadImageFromFile(fn, &img);
  gpcheck("load image", s);

  GpTexture * gt = 0;
  s = GdipCreateTexture(img, WrapModeTile, &gt);
  gpcheck("texture", s);
  if (!tiled) {
    uint iw, ih;
    s = GdipGetImageWidth(img, &iw);
    gpcheck("width", s);
    s = GdipGetImageHeight(img, &ih);
    gpcheck("height", s);
    s = GdipScaleTextureTransform(gt, (float)w / iw, (float)h / ih, 0);
    gpcheck("scale", s);
  }
  s = GdipDisposeImage(img);
  gpcheck("dispose img", s);

  bgbrush_img = gt;
#endif
}

static bool
fill_rect(HDC dc, RECT * boxp, GpBrush * br)
{
  GpStatus s, sbrush = -1;
#ifdef debug_gdiplus
  static int nfills = 0;
  nfills ++;
#endif

  void fill(void)
  {
    sbrush = GdipFillRectangleI(bg_graphics, br, boxp->left, boxp->top, boxp->right - boxp->left, boxp->bottom - boxp->top);
    gpcheck("fill", sbrush);
  }

  if (bg_graphics) {
    fill();
  }
  if (sbrush != Ok) {
    if (bg_graphics) {
      s = GdipDeleteGraphics(bg_graphics);
      gpcheck("delete graphics", s);
      bg_graphics = 0;
    }
#ifdef debug_gdiplus
    printf("creating graphics, failure rate 1/%d\n", nfills);
    nfills = 0;
#endif
    s = GdipCreateFromHDC(dc, &bg_graphics);
    gpcheck("create graphics", s);
    fill();
  }

  return sbrush == Ok;
}

#endif

void
win_flush_background(bool clearbg)
{
#ifdef debug_gdiplus
  printf("flush background bmp %d img %d gr %d (tiled %d)\n", !!bgbrush_bmp, !!bgbrush_img, !!bg_graphics, tiled);
#endif
  w = 0; h = 0;
  tiled = false;
  if (clearbg) {
    alpha = -1;
    // TODO: save redundant image reloading (and brush creation)
  }

  if (bgbrush_bmp) {
    DeleteObject(bgbrush_bmp);
    bgbrush_bmp = 0;
  }
#if CYGWIN_VERSION_API_MINOR >= 74
  drop_background_image_brush();
  GpStatus s;
  if (bg_graphics) {
    s = GdipDeleteGraphics(bg_graphics);
    bg_graphics = 0;
    gpcheck("delete graphics", s);
  }
#endif
}

/*
   Return background image filename in malloced string.
 */
static wchar *
get_bg_filename(void)
{
  tiled = false;
  ratio = false;
  wallp = false;
  static wchar * wallpfn = 0;

  wchar * bgfn = (wchar *)cfg.background;
  if (*bgfn == '*') {
    tiled = true;
    bgfn++;
  }
  else if (*bgfn == '%') {
    ratio = true;
    bgfn++;
  }
  else if (*bgfn == '_') {
    bgfn++;
  }
#if CYGWIN_VERSION_API_MINOR >= 74
  else if (*bgfn == '=') {
    wallp = true;

    if (!wallpfn)
      wallpfn = newn(wchar, MAX_PATH + 1);

    void readregstr(HKEY key, wstring attribute, wchar * val, DWORD len) {
      DWORD type;
      int err = RegQueryValueExW(key, attribute, 0, &type, (void *)val, &len);
      if (err ||
          !(type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ)
         )
        *val = 0;
    }

    HKEY wpk = 0;
    RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\Desktop", &wpk);
    if (wpk) {
      readregstr(wpk, W("Wallpaper"), wallpfn, MAX_PATH);
      wchar regval[22];
      readregstr(wpk, W("TileWallpaper"), regval, lengthof(regval));
      tiled = 0 == wcscmp(regval, W("1"));
      readregstr(wpk, W("WallpaperStyle"), regval, lengthof(regval));
      wallp_style = wcstol(regval, 0, 0);
      //printf("wallpaper <%ls> tiled %d style %d\n", wallpfn, tiled, wallp_style);

      if (tiled && !wallp_style) {
        // can be used as brush directly
      }
      else {
        // need to scale wallpaper later, when loading;
        // not implemented, invalidate
        *wallpfn = 0;
        // possibly, according to docs, but apparently ignored, 
        // also determine origin according to
        // readregstr(wpk, W("WallpaperOriginX"), ...)
        // readregstr(wpk, W("WallpaperOriginY"), ...)
      }
      RegCloseKey(wpk);
    }
  }
#else
  (void)wallp_style;
#endif

  char * bf = cs__wcstombs(bgfn);
  // try to extract an alpha value from file spec
  char * salpha = strrchr(bf, ',');
  if (salpha) {
    *salpha = 0;
    salpha++;
    if (sscanf(salpha, "%u%c", &alpha, &(char){0}) != 1)
      alpha = -1;
  }

  if (wallp)
    bgfn = wcsdup(wallpfn);
  else {
    // path transformations
    if (0 == strncmp("~/", bf, 2)) {
      char * bfexp = asform("%s/%s", home, bf + 2);
      free(bf);
      bf = bfexp;
    }
    else if (*bf != '/' && !(*bf && bf[1] == ':')) {
      char * fgd = foreground_cwd();
      if (fgd) {
        char * bfexp = asform("%s/%s", fgd, bf);
        free(bf);
        bf = bfexp;
      }
    }

    if (support_wsl && !wallp) {
      wchar * wbf = cs__utftowcs(bf);
      wchar * wdewbf = dewsl(wbf);  // free(wbf)
      char * dewbf = cs__wcstoutf(wdewbf);
      free(wdewbf);
      free(bf);
      bf = dewbf;
    }

    bgfn = path_posix_to_win_w(bf);
  }

#ifdef debug_gdiplus
  printf("loading brush <%ls> <%s> <%ls>\n", cfg.background, bf, bgfn);
#endif
  free(bf);
  return bgfn;
}

static void
load_background_brush(HDC dc)
{
  // we could try to hook into win_adapt_term_size to update the full 
  // screen background and reload the background on demand, 
  // but let's rather handle this autonomously here
  RECT cr;
  GetClientRect(wnd, &cr);
  if (cr.right - cr.left == w && cr.bottom - cr.top == h)
    return;  // keep brush

  if (tiled)
    return;  // do not scale tiled brush

  // remember terminal screen size
  w = cr.right - cr.left;
  h = cr.bottom - cr.top;

  // adjust paint screen size
  if (win_search_visible())
    cr.bottom -= SEARCHBAR_HEIGHT;

  // support tabbar (minor need here)
  cr.top += OFFSET;

  wchar * bgfn = get_bg_filename();  // also set tiled and alpha

  HBITMAP
  load_background_bitmap(wstring fn)
  {
    HBITMAP bm = 0;
    wstring bmpsuf = wcscasestr(fn, W(".bmp"));
    if (bmpsuf && wcslen(bmpsuf) == 4) {
      if (tiled)
        bm = (HBITMAP) LoadImageW(0, fn,
                                  IMAGE_BITMAP, 0, 0,
                                  LR_DEFAULTSIZE |
                                  LR_LOADFROMFILE);
      else
        bm = (HBITMAP) LoadImageW(0, fn,
                                  IMAGE_BITMAP, w, h,
                                  LR_LOADFROMFILE);
    }

    if (bm && alpha >= 0) {
      if (tiled)
        bm = alpha_blend_bg(alpha, dc, bm, 0, 0, win_get_colour(BG_COLOUR_I));
      else
        bm = alpha_blend_bg(alpha, dc, bm, w, h, win_get_colour(BG_COLOUR_I));
    }

    return bm;
  }

  if (!bgbrush_bmp) {
    HBITMAP bm = load_background_bitmap(bgfn);
    if (bm) {
      bgbrush_bmp = CreatePatternBrush(bm);
      DeleteObject(bm);
      if (bgbrush_bmp) {
        FillRect(dc, &cr, bgbrush_bmp);
      }
    }
  }

  if (!bgbrush_bmp) {
#if CYGWIN_VERSION_API_MINOR >= 74
    load_background_image_brush(dc, bgfn);
    // can have set bgbrush_img or bgbrush_bmp
    if (bgbrush_img)
      fill_rect(dc, &cr, bgbrush_img);
    // flag failure to load background?
    // this is now detected in win_paint by checking the brushes
    //else if (!bgbrush_bmp)
#endif
    //  // trigger proper win_paint behaviour
    //  wstrset(&cfg.background, W(""));  // not the right approach (zooming)
  }
#ifdef debug_gdiplus
  printf("loaded brush <%ls>: GDI %d GDI+ %d (tiled %d)\n", bgfn, !!bgbrush_bmp, !!bgbrush_img, tiled);
#endif

  free(bgfn);
}

bool
fill_background(HDC dc, RECT * boxp)
{
  load_background_brush(dc);
  if (wallp) {
    offset_bg(dc);
  }

  // support tabbar
  if (boxp->top < OFFSET)
    boxp->top = OFFSET;

  return
    (bgbrush_bmp && FillRect(dc, boxp, bgbrush_bmp))
#if CYGWIN_VERSION_API_MINOR >= 74
    || (bgbrush_img && fill_rect(dc, boxp, bgbrush_img))
#endif
    ;
}

#define dont_debug_aspect_ratio

void
scale_to_image_ratio()
{
#if CYGWIN_VERSION_API_MINOR >= 74
  if (*cfg.background != '%')
    return;
  wchar * bgfn = get_bg_filename();
#ifdef debug_aspect_ratio
  printf("scale_to_image_ratio <%ls> ratio %d\n", bgfn, ratio);
#endif
  if (!ratio)
    return;

  uint bw, bh;
  int res = get_image_size(bgfn, &bw, &bh);
  free(bgfn);
  if (!res || !bw || !bh)
    return;

  RECT cr;
  GetClientRect(wnd, &cr);
  // remember terminal screen size
  int w = cr.right - cr.left;
  int h = cr.bottom - cr.top;
#ifdef debug_aspect_ratio
  printf("  cur w %d h %d img bw %d bh %d\n", (int)w, (int)h, bw, bh);
#endif

  if (abs((int)bw * h - (int)bh * w) < w + h)
    return;

  w = max(w, ini_width);
  h = max(h, ini_height);
#ifdef debug_aspect_ratio
  printf("  max w %d h %d\n", (int)w, (int)h);
#endif

  int xh, xw;
  if (bw * h < bh * w) {
    xh = h;
    xw = bw * xh / bh;
  } else {
    xw = w;
    xh = bh * xw / bw;
  }
  int sy = win_search_visible() ? SEARCHBAR_HEIGHT : 0;
#ifdef debug_aspect_ratio
  printf("  %dx%d (%dx%d) -> %dx%d\n", (int)w, (int)h, bw, bh, xw, xh);
#endif
  // rescale window to aspect ratio of background image
  win_set_pixels(xh - 2 * PADDING - OFFSET - sy, xw - 2 * PADDING);
#endif
}


/*
   Text output.
 */

#define dont_debug_win_text

#ifdef debug_win_text
static void
_trace_line(char * tag, cattr attr, ushort lattr, wchar * text, int len)
{
  bool show = false;
  for (int i = 0; i < len; i++)
    if (text[i] != ' ')
      show = true;
  if (show) {
    if (*tag != ' ') {
      wchar t[len + 1]; wcsncpy(t, text, len); t[len] = 0;
      printf("%s %04X %08llX <%ls>\n", tag, lattr, attr.attr, t);
    }
    else {
      printf("%s %04X %08llX", tag, lattr, attr.attr);
      for (int i = 0; i < len; i++) printf(" %04X", text[i]);
      printf("\n");
    }
  }
}
#define trace_line(tag) _trace_line(tag, attr, lattr, text, len)
#else
#define trace_line(tag)
#endif

static wchar
combsubst(wchar comb, cattrflags attr)
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
      win_check_glyphs(&chk, 1, attr);
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

static void
text_out_start(HDC hdc, LPCWSTR psz, int cch, int *dxs)
{
  if (cch == 0)
    use_uniscribe = false;
  if (!use_uniscribe)
    return;

#if CYGWIN_VERSION_API_MINOR >= 74
  static SCRIPT_CONTROL sctrl_lig = (SCRIPT_CONTROL){.fMergeNeutralItems = 1};
#else
  SCRIPT_CONTROL sctrl_lig = (SCRIPT_CONTROL){.fReserved = 1};
#endif
  HRESULT hr = ScriptStringAnalyse(hdc, psz, cch, 0, -1, 
    // could | SSA_FIT and use `width` (from win_text) instead of MAXLONG
    // to justify to monospace cell widths;
    // SSA_LINK is needed for Hangul and default-size CJK
    SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, MAXLONG, 
    cfg.ligatures > 1 ? &sctrl_lig : 0, 
    NULL, dxs, NULL, NULL, &ssa);
  if (!SUCCEEDED(hr) && hr != USP_E_SCRIPT_NOT_IN_FONT)
    use_uniscribe = false;
}

static void
text_out(HDC hdc, int x, int y, UINT fuOptions, RECT *prc, LPCWSTR psz, int cch, int *dxs)
{
  if (cch == 0)
    return;
#ifdef debug_text_out
  if (*psz >= 0x80) {
    printf("@%3d (%3d):", x, y);
    for (int i = 0; i < cch; i++)
      printf(" %04X<%d>", psz[i], dxs[i]);
    printf("\n");
  }
#endif

  if (use_uniscribe)
    ScriptStringOut(ssa, x, y, fuOptions, prc, 0, 0, FALSE);
  else
    ExtTextOutW(hdc, x, y, fuOptions, prc, psz, cch, dxs);
}

static void
text_out_end()
{
  if (use_uniscribe)
    ScriptStringFree(&ssa);
}


// applies bold as colour if required, returns true if still needs thickening
static bool
old_apply_bold_colour(colour_i *pfgi)
{
  // We use two bits to control colouring and thickening for three classes of
  // colours: ANSI (0-7), default, and other (8-256, true). We also reserve one
  // combination for xterm's default (thicken everything, ANSI is also coloured).
  // - "other" is always thickened and never gets colouring.
  // - when bold_as_colour=no: ANSI+defaut are only thickened.
  // - when bold_as_colour=yes:
  //   .. and bold_as_font=no:  ANSI+default are only coloured
  //   .. and bold_as_font=yes: ANSI+default are thickened, ANSI also coloured (xterm)
  if (cfg.bold_as_colour) {
    if (CCL_ANSI8(*pfgi)) {
      *pfgi |= 8;     // (BLACK|...|WHITE)_I -> BOLD_(BLACK|...|WHITE)_I
      return cfg.bold_as_font;
    }
    if (CCL_DEFAULT(*pfgi) && !cfg.bold_as_font) {
      *pfgi |= 1;     // (FG|BG)_COLOUR_I -> BOLD_(FG|BG)_COLOUR_I
      return false;
    }
  }
  return true;
}

// removes default colours if not reversed, returns true if still needs thickening
static bool
old_rtf_bold_decolour(cattrflags attr, colour_i * pfgi, colour_i * pbgi)
{
  bool bold_thickens = cfg.bold_as_font;  // all colours
  // if not reverse:
  // - if only bold_as_colour, ATTR_BOLD still thickens default fg on default bg
  // - don't colour default fg/bg (caller interprets COLOUR_NUM as "no colour").
  if (!(attr & ATTR_REVERSE)) {
    if (cfg.bold_as_colour && CCL_DEFAULT(*pfgi) && CCL_DEFAULT(*pbgi))
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
static cattr
old_apply_attr_colour(cattr a, attr_colour_mode mode)
{
  // indexed modifications
  bool do_reverse_i = mode & (ACM_RTF_PALETTE | ACM_RTF_GEN);
  bool do_bold_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN | ACM_SIMPLE | ACM_VBELL_BG);
#ifdef handle_blinking_here
  bool do_blink_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN);
#endif
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
    reset_bold = !old_apply_bold_colour(&fgi);  // we'll reset afterwards if needed

#ifdef handle_blinking_here
  // this is handled in term_paint
  if (do_blink_i && (a.attr & ATTR_BLINK)) {
    if (CCL_ANSI8(bgi))
      bgi |= 8;
    else if (CCL_DEFAULT(bgi))
      bgi |= 1;
  }
#endif

  if (do_finalize_rtf_i) {
    if (do_rtf_bold_decolour_i) {  // uses ATTR_BOLD, ATTR_REVERSE
      bool thicken = old_rtf_bold_decolour(a.attr, &fgi, &bgi);
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
    bg = brighten(bg, fg, false);

  // ACM_TERM does also search and cursor colours. for now we don't handle those

  a.truefg = fg;
  a.truebg = bg;
  a.attr |= TRUE_COLOUR << ATTR_FGSHIFT | TRUE_COLOUR << ATTR_BGSHIFT;
  return a;
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
  // switchable attribute colours
  if (term.enable_bold_colour && CCL_DEFAULT(*pfgi)
      && colours[BOLD_COLOUR_I] != (colour)-1
     )
    *pfgi = BOLD_COLOUR_I;
  else if (term.enable_blink_colour && CCL_DEFAULT(*pfgi)
      && colours[BLINK_COLOUR_I] != (colour)-1
     )
    *pfgi = BLINK_COLOUR_I;
  else
  // normal independent as_font/as_colour controls
  if (cfg.bold_as_colour) {
    if (ansi)
      *pfgi |= 8;
    else  // default
      *pfgi |= 1;  // (FG|BG)_COLOUR_I -> BOLD_(FG|BG)_COLOUR_I
  }
  return cfg.bold_as_font;  // thicken if bold_as_font
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
  if (cfg.old_bold)
    return old_apply_attr_colour(a, mode);

  // indexed modifications
  bool do_reverse_i = mode & (ACM_RTF_PALETTE | ACM_RTF_GEN);
  bool do_bold_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN | ACM_SIMPLE | ACM_VBELL_BG);
#ifdef handle_blinking_here
  bool do_blink_i = mode & (ACM_TERM | ACM_RTF_PALETTE | ACM_RTF_GEN);
#endif
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

#ifdef handle_blinking_here
  // this is handled in term_paint
  if (do_blink_i && (a.attr & ATTR_BLINK)) {
    if (CCL_ANSI8(bgi))
      bgi |= 8;
    else if (CCL_DEFAULT(bgi))
      bgi |= 1;
  }
#endif

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
    bg = brighten(bg, fg, false);

  // ACM_TERM does also search and cursor colours. for now we don't handle those

  if (a.attr & TATTR_CLEAR)
    bg = brighten(bg, fg, false);

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
   clearpad: flag to clear padding from overhang
   phase: overlay line display (italic right-to-left overhang handling)
 */
void
win_text(int tx, int ty, wchar *text, int len, cattr attr, cattr *textattr, ushort lattr, bool has_rtl, bool clearpad, uchar phase)
{
#ifdef debug_wscale
  if (attr.attr & (TATTR_EXPAND | TATTR_NARROW | TATTR_WIDE))
    for (int i = 0; i < len; i++)
      printf("[%2d:%2d] %c%c%c%c %04X\n", ty, tx + i, attr.attr & TATTR_NARROW ? 'n' : ' ', attr.attr & TATTR_EXPAND ? 'x' : ' ', attr.attr & TATTR_WIDE ? 'w' : ' ', " WUL"[lattr & LATTR_MODE], text[i]);
#endif
  //if (kb_trace) {printf("[%ld] <win_text\n", mtime()); kb_trace = 0;}

  int graph = (attr.attr & GRAPH_MASK) >> ATTR_GRAPH_SHIFT;
  bool graph_vt52 = false;
  bool powerline = false;
  int findex = (attr.attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  // in order to save attributes bits, special graphic handling is encoded 
  // in a compact form, combined with unused values of the font number;
  // here we do some decoding and also recoding back to the previous format, 
  // in order to avoid micro-refactoring in the code below, 
  // where graphic encoding is interpreted bitwise in some cases;
  // the font ranges used are:
  //  <none> (graph part only): special for DEC Technical, VT52 fraction
  //  11     VT100 Line Drawing, bit-wise encoded segments
  //  12     VT100 scanlines 1...5
  //  13     VT52 scanlines 1...8
  //  14     Unicode Block Elements, part 1
  //  15     Unicode Block Elements, part 2
  if (findex > 10) {
    if (findex == 12) // VT100 scanlines
      graph <<= 4;
    else if (findex == 13 && graph == 15) { // Private: geometric Powerline
      graph = 0;
      powerline = true;
    }
    else if (findex == 13) { // VT52 scanlines
      graph <<= 4;
      graph_vt52 = true;
    }
    else if (findex >= 14)
      graph |= 0x80 | ((findex & 1) << 4);

    findex = 0;
  }
  else if (graph) {
    if (graph == 0xF)
      graph = 0xF7;
    else if (graph == 0xE)
      graph = 0xE0;
    else
      graph |= 0xE0;
  }
  struct fontfam * ff = &fontfamilies[findex];

  trace_line("win_text:");

  bool ldisp1 = phase == 1;
  bool ldisp2 = phase == 2;
  bool lpresrtl = lattr & LATTR_PRESRTL;
  lattr &= LATTR_MODE;

  int char_width = cell_width * (1 + (lattr != LATTR_NORM));

 /* Only want the left half of double width lines */
  // check this before scaling up x to pixels!
  if (lattr != LATTR_NORM && tx * 2 >= term.cols)
    return;

 /* Convert to window coordinates */
  int x = tx * char_width + PADDING;
  int y = ty * cell_height + OFFSET + PADDING;

#ifdef support_triple_width
#define TATTR_TRIPLE 0x0080000000000000u
  if ((attr.attr & TATTR_TRIPLE) == TATTR_TRIPLE) {
    char_width *= 3;
    attr.attr &= ~TATTR_TRIPLE;
  }
  else
#endif
  if (attr.attr & TATTR_WIDE)
    char_width *= 2;

  bool wscale_narrow_50 = false;
  if ((attr.attr & (TATTR_NARROW | TATTR_CLEAR)) == (TATTR_NARROW | TATTR_CLEAR)) {
    // indicator for adjustment of auto-narrowing;
    // geometric Powerline symbols, explicit single-width attribute narrowing
    attr.attr &= ~TATTR_CLEAR;
    wscale_narrow_50 = true;
  }

  bool default_bg = (attr.attr & ATTR_BGMASK) >> ATTR_BGSHIFT == BG_COLOUR_I;
  if (attr.attr & ATTR_REVERSE)
    default_bg = false;
  attr = apply_attr_colour(attr, ACM_TERM);
  colour fg = attr.truefg;
  colour bg = attr.truebg;
  // ATTR_BOLD is now set if and only if we need further thickening.

  bool has_cursor = attr.attr & (TATTR_ACTCURS | TATTR_PASCURS);
  colour cursor_colour = 0;

#ifdef keep_sel_colour_here
  if ((attr.attr & ATTR_BGMASK) >> ATTR_BGSHIFT == SEL_COLOUR_I)
#else
  if (attr.attr & TATTR_SELECTED)
#endif
    default_bg = false;

  if (attr.attr & (TATTR_CURRESULT | TATTR_CURMARKED)) {
    bg = cfg.search_current_colour;
    fg = cfg.search_fg_colour;
    default_bg = false;
  }
  else if (attr.attr & (TATTR_RESULT | TATTR_MARKED)) {
    bg = cfg.search_bg_colour;
    fg = cfg.search_fg_colour;
    default_bg = false;
  }

  if (has_cursor) {
    if (term_cursor_type() == CUR_BLOCK && (attr.attr & TATTR_ACTCURS))
      default_bg = false;

    cursor_colour = colours[ime_open ? IME_CURSOR_COLOUR_I : CURSOR_COLOUR_I];
    //printf("cc (ime_open %d) %06X\n", ime_open, cursor_colour);

    //static uint mindist = 32768;
    static uint mindist = 22222;
    //static uint mindist = 8000;
    bool too_close = colour_dist(cursor_colour, bg) < mindist;

    if (too_close) {
      //cursor_colour = fg;
      colour ccfg = brighten(cursor_colour, fg, false);
      colour ccbg = brighten(cursor_colour, bg, false);
      if (colour_dist(ccfg, bg) < mindist
          && colour_dist(ccfg, bg) < colour_dist(ccbg, bg)
         )
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
      printf("set cursor (colour %06X) @(row %d col %d) cursor_on %d\n", bg, (y - PADDING - OFFSET) / cell_height, (x - PADDING) / char_width, term.cursor_on);
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

  int wscale = 100;

  if (attr.attr & TATTR_EXPAND) {
    if (nfont & FONT_WIDE)
      wscale = 200;
    nfont |= FONT_WIDE;
  }
  else if (wscale_narrow_50)
    wscale = 50;
#ifndef narrow_via_font
  else if ((attr.attr & TATTR_NARROW) && !(attr.attr & TATTR_ZOOMFULL)) {
    wscale = cfg.char_narrowing;
    if (wscale > 100)
      wscale = 100;
    if (wscale < 50)
      wscale = 50;
    nfont |= FONT_NARROW;
  }
#endif

  bool do_special_underlay = false;
  if (cfg.bold_as_special && (attr.attr & ATTR_BOLD)) {
    do_special_underlay = true;
    attr.attr &= ~ATTR_BOLD;
  }
  if (ff->bold_mode == BOLD_FONT && (attr.attr & ATTR_BOLD))
    nfont |= FONT_BOLD;
  if (ff->und_mode == UND_FONT && (attr.attr & UNDER_MASK) == ATTR_UNDER
      && !(attr.attr & ATTR_ULCOLOUR)
     )
    nfont |= FONT_UNDERLINE;
  if (attr.attr & ATTR_ITALIC)
    nfont |= FONT_ITALIC;
  if (attr.attr & ATTR_STRIKEOUT
      && !cfg.underl_manual && cfg.underl_colour == (colour)-1
      && !(attr.attr & ATTR_ULCOLOUR)
     )
    nfont |= FONT_STRIKEOUT;
  if (attr.attr & TATTR_ZOOMFULL)
    nfont |= FONT_ZOOMFULL;
  if (attr.attr & (ATTR_SUBSCR | ATTR_SUPERSCR))
    nfont |= FONT_ZOOMSMALL;
  if (attr.attr & TATTR_SINGLE)
    nfont |= FONT_ZOOMDOWN;
  another_font(ff, nfont);

  bool force_manual_underline = false;
  if (!ff->fonts[nfont]) {
    if (nfont & FONT_UNDERLINE)
      force_manual_underline = true;
    // Don't force manual bold, it could be bad news.
    nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
  }
#ifdef narrow_via_font
  if ((nfont & (FONT_WIDE | FONT_NARROW)) == (FONT_WIDE | FONT_NARROW))
    nfont &= ~(FONT_WIDE | FONT_NARROW);
#endif

  another_font(ff, nfont);
  if (!ff->fonts[nfont])
    nfont = FONT_NORMAL;

#if defined(debug_bold) && debug_bold > 1
  wchar t[len + 1]; wcsncpy(t, text, len); t[len] = 0;
  printf("font %02X (%dpt) bold_mode %d attr_bold %d fg %06X <%ls>\n", nfont, font_size, ff->bold_mode, !!(attr.attr & ATTR_BOLD), fg, t);
#endif

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

    trace_line(" <ChrPlc:");
    // This does not work for non-BMP:
    GetCharacterPlacementW(dc, text, len, 0, &gcpr,
                           FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);
    len = gcpr.nGlyphs;
    trace_line(" >ChrPlc:");
    eto_options |= ETO_GLYPH_INDEX;
  }


  bool combining = attr.attr & TATTR_COMBINING;
  bool combining_double = attr.attr & TATTR_COMBDOUBL;

  bool let_windows_combine = false;
  if (combining) {
   /* Substitute combining characters by overprinting lookalike glyphs */
    for (int i = 0; i < len; i++)
      text[i] = combsubst(text[i], attr.attr);
   /* Determine characters that should be combined by Windows */
    if (len == 2) {
      if (text[0] == 'i' && (text[1] == 0x030F || text[1] == 0x0311))
        let_windows_combine = true;
     /* Enforce separate combining characters display if colours differ */
      if (!termattrs_equal_fg(&textattr[1], &attr))
        let_windows_combine = false;
    }
  }

  wchar * origtext = 0;
  if (graph && graph < 0xE0) {
    // clear codes for Block Elements, VT100 Line Drawing and "scanlines"
    for (int i = 0; i < len; i++)
      text[i] = ' ';
  }
  else if (powerline) {
    origtext = newn(wchar, len);
    // clear codes for Powerline geometric symbols
    for (int i = 0; i < len; i++) {
      origtext[i] = text[i];
      text[i] = ' ';
    }
  }

 /* Array with offsets between neighbouring characters */
  int dxs[len];
  int dx = combining ? 0 : char_width;
  for (int i = 0; i < len; i++) {
    if (is_high_surrogate(text[i]))
      // This does not have the expected effect so we keep splitting up 
      // non-BMP characters into single character chunks for now (term.c)
      dxs[i] = 0;
    else
      dxs[i] = dx;
  }

 /* Character cells length */
  int ulen = 0;
  for (int i = 0; i < len; i++) {
    ulen++;
    if (char1ulen(&text[i]) == 2)
      i++;  // skip low surrogate;
  }

 /* Painting box */
  int width = char_width * (combining ? 1 : ulen);
  RECT box = {
    .top = y, .bottom = y + cell_height,
    .left = x, .right = min(x + width, cell_width * term.cols + PADDING)
  };
  RECT box0 = box;
  if (ldisp2) {  // e.g. attr.attr & ATTR_ITALIC
    box.right += cell_width;
    box.left -= cell_width;
  }
  if (clearpad && tx > 0)
    box.right += PADDING;
  RECT box2 = box;
  if (combining_double)
    box2.right += char_width;


 /* Uniscribe handling */
  use_uniscribe = cfg.font_render == FR_UNISCRIBE && !has_rtl;
  if (combining_double)
    use_uniscribe = false;
#ifdef no_Uniscribe_for_ASCII_only_chunks
  // this "optimization" was intended to avoid a performance penalty 
  // for Uniscribe when there is no need for Uniscribe;
  // however, even for ASCII-only chunks, 
  // Uniscribe effectively applies ligatures (Fira Code, #601), 
  // and testing again, there is hardly a penalty observable anymore
  if (use_uniscribe) {
    use_uniscribe = false;
    for (int i = 0; i < len; i++)
      if (text[i] >= 0x80) {
        use_uniscribe = true;
        break;
      }
  }
#endif

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

 /* Determine shadow/overstrike bold or double-width/height width */
  int xwidth = 1;
  if (ff->bold_mode == BOLD_SHADOW && (attr.attr & ATTR_BOLD)) {
    // This could be scaled with font size, but at risk of clipping
    xwidth = 2;
    if (lattr != LATTR_NORM) {
      xwidth = 3; // 4?
    }
  }

 /* Manual underline */
  colour ul = fg;
  int uloff = ff->descent + (cell_height - ff->descent + 1) / 2;
  if (lattr == LATTR_BOT)
    uloff = ff->descent + (cell_height - ff->descent + 1) / 2;
  uloff += line_width / 2;
  if (uloff >= cell_height)
    uloff = cell_height - 1;

  if (attr.attr & ATTR_ULCOLOUR)
    ul = attr.ulcolr;
  else if (cfg.underl_colour != (colour)-1)
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
    ul = 0x802020E0;
  }
  else if (nfont & FONT_BOLD) {
    force_manual_underline = true;
    ul = 0x8020E020;
  }
#endif

  bool underlaid = false;
  void clear_run() {
    if (!underlaid) {
      ExtTextOutW(dc, xt, yt, eto_options | ETO_OPAQUE, &box0, W(" "), 1, dxs);

      underlaid = true;
    }
  }

 /* Graphic background: picture or texture */
  if (*cfg.background && default_bg) {
    RECT bgbox = box0;

    // extend into padding area
    if (!tx)
      bgbox.left = 0;
    if (bgbox.right >= PADDING + cell_width * term.cols)
      bgbox.right += PADDING;
    if (!ty)
      bgbox.top = 0;
    if (ty == term.rows - 1) {
      RECT cr;
      GetClientRect(wnd, &cr);
      if (win_search_visible())
        cr.bottom -= SEARCHBAR_HEIGHT;
      bgbox.bottom = cr.bottom;
    }

    if (fill_background(dc, &bgbox))
      underlaid = true;
#ifdef debug_text_background
    if (*text != 'x')
      underlaid = false;
#endif
  }

 /* Coordinate transformation per line */
  int coord_transformed = 0;
  XFORM old_xform;
  if (lpresrtl) {
    coord_transformed = SetGraphicsMode(dc, GM_ADVANCED);
    if (coord_transformed && GetWorldTransform(dc, &old_xform)) {
      XFORM xform = (XFORM){-1.0, 0.0, 0.0, 1.0, term.cols * cell_width + 2 * PADDING, 0.0};
      coord_transformed = SetWorldTransform(dc, &xform);
    }
  }

 /* Special underlay */
  if (do_special_underlay && !ldisp2) {
    xchar uc = 0x2312;
    int ulaylen = uc > 0xFFFF ? ulen * 2 : ulen;
    wchar ulay[ulaylen];
    for (int i = 0; i < ulaylen; i++)
      if (uc > 0xFFFF)
        if (i & 1)
          ulay[i] = low_surrogate(uc);
        else
          ulay[i] = high_surrogate(uc);
      else
        ulay[i] = uc;

    static colour rainbow[] = { // https://en.wikipedia.org/wiki/Rainbow
      RGB(0xFF, 0x00, 0x00), // red
      RGB(0xFF, 0x66, 0x00), // orange
      RGB(0xFF, 0xEE, 0x00), // yellow
      RGB(0x00, 0xFF, 0x00), // green
      RGB(0x00, 0x99, 0xFF), // blue
      RGB(0x44, 0x00, 0xFF), // indigo
      RGB(0x99, 0x00, 0xFF), // violet
      };

    int dist = cell_height / 20 + 1;
    int leap = cell_height <= 20 ? 3 : cell_height <= 30 ? 2 : 1;
    int y = yt - cell_height / 2 + 4 * dist;
    for (uint c = 0; c < lengthof(rainbow); c += leap) {
      SetTextColor(dc, rainbow[c]);
      uint eto = eto_options;
      if (!underlaid && !c)
        eto |= ETO_OPAQUE;
      ExtTextOutW(dc, xt, y, eto, &box, ulay, ulaylen, dxs);
      y += dist;
    }
    SetTextColor(dc, bg);
    ExtTextOutW(dc, xt, y, eto_options, &box, ulay, ulaylen, dxs);
    SetTextColor(dc, fg);

    underlaid = true;
  }

  if (attr.attr & (ATTR_SUBSCR | ATTR_SUPERSCR)) {
    xt += cell_width * 3 / 10;
    if (attr.attr & ATTR_SUBSCR)
      yt += cell_height * 3 / 8;
    else
      yt += cell_height * 1 / 8;
  }
  if (attr.attr & TATTR_SINGLE)
    yt += cell_height / 4;

 /* Shadow */
  int layer = 0;
  colour fg0 = fg;
  colour ul0 = ul;
  if (attr.attr & ATTR_SHADOW) {
    layer = 1;
    xt += line_width;
    yt -= layer * line_width;
    x += line_width;
    y -= layer * line_width;
    fg = ((fg & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
    ul = ((ul & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
    SetTextColor(dc, fg);
  }

  int bloom = 0;
  XFORM old_xform_bloom;
  int coord_transformed_bloom = 0;
  if (cfg.bloom) {
    bloom = 2;
    fg = ((fg & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
    SetTextColor(dc, fg);
  }

draw:;
  if (bloom) {
    if (bloom > 1 || bloom >= 1)
      fg = ((fg & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
    else {
      colour fg2 = (fg & 0xFEFEFEFE) >> 1;
      colour bg2 = (win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1;
      colour fg4 = (fg & 0xFCFCFCFC) >> 2;
      colour bg4 = (win_get_colour(BG_COLOUR_I) & 0xFCFCFCFC) >> 2;
      fg = fg2 + fg4 + bg2 + bg4;
    }
    SetTextColor(dc, fg);
    SelectObject(dc, ff->fonts[nfont | FONT_BOLD]);

    coord_transformed_bloom = SetGraphicsMode(dc, GM_ADVANCED);
    if (coord_transformed_bloom && GetWorldTransform(dc, &old_xform_bloom)) {
      clear_run();

      float scale = 1.0 + (float)bloom / 7.0;
      /*
        xt' = xt + cell_width / 2
        x' - xt' = sc * (x - xt')
        x' = xt' + sc * x - sc * xt'
        x' = sc * x + (1 - sc) * xt'
      */
      XFORM xform = (XFORM){scale, 0.0, 0.0, scale, 
                    ((float)xt + (float)cell_width / 2) * (1.0 - scale), 
                    ((float)yt + (float)cell_height / 2) * (1.0 - scale)};
      coord_transformed_bloom = ModifyWorldTransform(dc, &xform, MWT_LEFTMULTIPLY);
    }
  }
#ifdef debug_draw
  if (*text != ' ')
    printf("draw @%d:%d %d:%d %d:%d\n", ty, tx, yt, xt, y, x);
#endif
  int yt0 = yt;
  int xt0 = xt;
  int y0 = y;
  int x0 = x;

 /* Wavy underline */
  if (!ldisp2 && lattr != LATTR_TOP &&
      (attr.attr & UNDER_MASK) == ATTR_CURLYUND
     )
  {
    clear_run();
    //printf("curly %d:%d %d:%d (w %d ulen %d cw %d)\n", ty, tx, y, x, width, ulen, char_width);

    int step = 4;  // horizontal step width
    int delta = 3; // vertical phase height
    int offset = 1; // offset up from uloff
    //int x0 = x - PADDING - (x - PADDING) % step + PADDING;
    //int rep = (ulen * char_width - 1) / step / 3 + 2;
    // when starting the undercurl at the current text start position,
    // the transitions between adjacent Bezier waves looks disrupted;
    // trying to align them by snapping to a previous point (- x... % step)
    // does not help, maybe Bezier curves should be studied first;
    // so in order to achieve uniform undercurls, we always start at 
    // the line beginning position (getting clipped anyway)
    int x0 = PADDING;
    int rep = (x - x0 + width + char_width) / step / 3;

    POINT bezier[1 + rep * 3];
    //printf("                      ");
    for (int i = 0; i <= rep * 3; i++) {
      bezier[i].x = x0 + i * step;
      int wave = (i % 3 == 2) ? delta : (i % 3 == 1) ? -delta : 0;
      bezier[i].y = y + uloff - offset + wave;
      //printf("  %04d:%04d%s", uloff - offset + wave, i * step, i % 3 ? "" : "\n");
    }

    HRGN ur = 0;
    GetClipRgn(dc, ur);
    IntersectClipRect(dc, box.left, box.top, box.right, box.bottom);

    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    for (int l = 0; l < line_width; l++) {
      if (l)
        for (int i = 0; i <= rep * 3; i++)
          bezier[i].y--;
      PolyBezier(dc, (const POINT *)bezier, 1 + rep * 3);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);

    SelectClipRgn(dc, ur);
  }
  else

 /* Underline */
  if (!ldisp2 && lattr != LATTR_TOP &&
      (force_manual_underline ||
       (attr.attr & (ATTR_DOUBLYUND | ATTR_BROKENUND)) ||
       ((attr.attr & UNDER_MASK) == ATTR_UNDER &&
        (ff->und_mode == UND_LINE || (attr.attr & ATTR_ULCOLOUR)))
      )
     )
  {
    clear_run();

    int penstyle = (attr.attr & ATTR_BROKENUND)
                   ? (attr.attr & ATTR_DOUBLYUND)
                     ? PS_DASH
                     : PS_DOT
                   : PS_SOLID;
    HPEN oldpen = SelectObject(dc, CreatePen(penstyle, 0, ul));
    int gapfrom = 0, gapdone = 0;
    int _line_width = line_width;
    if ((attr.attr & UNDER_MASK) == ATTR_DOUBLYUND) {
      if (line_width < 3)
        _line_width = 3;
      int gap = _line_width / 3;
      gapfrom = (_line_width - gap) / 2;
      gapdone = _line_width - gapfrom;
    }
    for (int l = 0; l < _line_width; l++) {
      if (l >= gapdone || l < gapfrom) {
        MoveToEx(dc, x, y + uloff - l, null);
        LineTo(dc, x + ulen * char_width, y + uloff - l);
      }
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

 /* Overline */
  if (!ldisp2 && lattr != LATTR_BOT && attr.attr & ATTR_OVERL) {
    clear_run();

    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    for (int l = 0; l < line_width; l++) {
      MoveToEx(dc, x, y + l, null);
      LineTo(dc, x + ulen * char_width, y + l);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

  int dxs_[len];

 /* Background for overhang overlay */
  if (ldisp1) {
    if (!underlaid)
      clear_run();
    goto _return;
  }

 /* DEC Tech adjustments */
  if (graph >= 0xE0) {  // adjust rendering of DEC Technical and VT52 fraction
    if ((graph & ~1) == 0xE8)  // left square bracket corners
      xt += line_width + 1;
    else if ((graph & ~1) == 0xEA)  // right square bracket corners
      xt -= line_width + 1;
    else if (graph == 0xE7)  // middle angle: don't display ╳, draw (below)
      for (int i = 0; i < len; i++)
        text[i] = ' ';
    else if (graph == 0xE0)  // square root base: rather draw (partially)
      for (int i = 0; i < len; i++)
        text[i] = 0x2502;
    else if (graph == 0xF7)  // VT52 fraction numerator
      yt -= line_width;
  }

 /* Coordinate transformation per character */
  int coord_transformed2 = 0;
  XFORM old_xform2;
  RECT box_, box2_;
  if (wscale != 100) {
    coord_transformed2 = SetGraphicsMode(dc, GM_ADVANCED);
    if (coord_transformed2 && GetWorldTransform(dc, &old_xform2)) {
      clear_run();

      float scale = (float)wscale / 100.0;
      XFORM xform = (XFORM){scale, 0.0, 0.0, 1.0, xt * (1.0 - scale), 0.0};
      coord_transformed2 = ModifyWorldTransform(dc, &xform, MWT_LEFTMULTIPLY);
      if (coord_transformed2) {
        for (int i = 0; i < len; i++)
          dxs_[i] = dxs[i];
        box_ = box;
        box2_ = box2;
        // compensate for the scaling
        for (int i = 0; i < len; i++)
          dxs[i] /= scale;
        if (wscale <= 100) {
          box.right /= scale;
          box2.right /= scale;
        }
        else { // evolutionary algorithm; don't ask why or how it works :/
        // extend bounding box by the scaling
          box.right *= scale;
          box2.right *= scale;
        }
      }
    }
  }

 /* Finally, draw the text */
  uint overwropt;
  if (ldisp2 || underlaid) {
    SetBkMode(dc, TRANSPARENT);
    overwropt = 0;
  }
  else {
    SetBkMode(dc, OPAQUE);
    overwropt = ETO_OPAQUE;
  }
  trace_line(" TextOut:");
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
          if (layer)
            fg = ((fg & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
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

  if (coord_transformed2) {
    SetWorldTransform(dc, &old_xform2);
    // restore these in case we're in a shadow loop
    for (int i = 0; i < len; i++)
      dxs[i] = dxs_[i];
    box = box_;
    box2 = box2_;
  }

 /* Manual drawing of certain graphics */
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

  if (graph >= 0xE0) {  // fix DEC Technical characters, draw VT52 fraction
    if ((graph & 0x0C) == 0x08) {
      // square bracket corners already repositioned above
    }
    else {  // Sum segments to be (partially) drawn, 
            // square root base, pointing triangles, VT52 fraction numerator
      int sum_width = line_width;
      int y0 = (lattr == LATTR_BOT) ? y - cell_height : y;
      int yoff = (cell_height - line_width) * 3 / 5;
      if (lattr >= LATTR_TOP)
        yoff *= 2;
      int xoff = (char_width - line_width) / 2;
      // 0xE0 square root base
      // sum segments:
      // 0xE1 upper left: add diagonal to bottom
      // 0xE2 lower left: add diagonal to top
      // 0xE5 upper right: add hook down
      // 0xE6 lower right: add hook up
      // 0xE7 middle right angle
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
      if (graph <= 0xE2) {  // diagonals
        sum_width ++;
        xl += line_width - 1;
        xr = x + char_width - 1;
        if (graph == 0xE2) {
          int xb = xl;
          xl = xr;
          xr = xb;
        }
      }
      if (graph == 0xF7) {  // VT52 fraction numerator
        yt = y + (cell_height - line_width) * 10 / 16;
        yb = y + (cell_height - line_width) * 8 / 16;
        xl = x + line_width - 1;
        xr = xl + char_width - 1;
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
      // pointing triangles:
      if ((graph & ~1) == 0xEC) {
        xl = x + line_width;
        xr = x + char_width - 2 * line_width - 1;
        x0 = x + (char_width - line_width) / 2;
        yt = y + cell_height - 2 * line_width - 1;
        yb = y + line_width;
        if (graph & 1) {
          yt ^= yb;
          yb ^= yt;
          yt ^= yb;
        }
      }
      // special adjustments:
      if (graph == 0xE0) {  // square root base
        xl --;
      }
      else if (graph == 0xE7) {  // sum middle right angle
      }
      // draw:
      //HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, sum_width, fg));
      HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, line_width, fg));
      sum_width = 1;  // now handled by pen width
      for (int i = 0; i < len; i++) {
        for (int l = 0; l < sum_width; l++) {
          if (graph == 0xE0) {  // square root base
            MoveToEx(dc, xl + l, ycellb, null);
            LineTo(dc, xl - (xl - x0) / 2 + l, yb + l);
            LineTo(dc, x0, yb + l);
          }
          else if (graph == 0xE7) {  // sum middle right angle
            MoveToEx(dc, x0 + l, y0, null);
            LineTo(dc, xl + l, yt);
            LineTo(dc, x0 + l, yb);
          }
          else if ((graph & ~1) == 0xEC) {  // pointing triangles
            MoveToEx(dc, xl + l, yt, null);
            LineTo(dc, xr + l, yt);
            LineTo(dc, x0 + l, yb);
            LineTo(dc, xl + l, yt);
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
  else if ((graph >= 0x80 && !graph_vt52) || powerline) {  // drawn graphics
    // Block Elements (U+2580-U+259F)
    // ▀▁▂▃▄▅▆▇█▉▊▋▌▍▎▏▐░▒▓▔▕▖▗▘▙▚▛▜▝▞▟
    // Private Use geometric Powerline symbols (U+E0B0-U+E0BF, not 5, 7)
    // 
    //      - -
    int char_height = cell_height;
    if (lattr >= LATTR_TOP)
      char_height *= 2;
    int y0 = y;
    if (lattr == LATTR_BOT)
      y0 -= cell_height;
    int xi = x;
    //printf("x %d y %d w %d h %d cw %d \n", x, y, cell_width, cell_height, char_width);

    /*
       Mix fg at mix/8 with bg.
     */
    colour colmix(char mix)
    {
      uint r = (red(fg) * mix + red(bg) * (8 - mix)) / 8;
      uint g = (green(fg) * mix + green(bg) * (8 - mix)) / 8;
      uint b = (blue(fg) * mix + blue(bg) * (8 - mix)) / 8;
      colour res = RGB(r, g, b);
      if (layer)
        res = ((res & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
      return res;
    }
    void linedraw(char l, char t, char r, char b, colour c)
    {
      HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, c));
      //printf("line %d %d %d %d\n", xi + l, y0 + t, xi + r, y0 + b);
      MoveToEx(dc, xi + l, y0 + t, null);
      LineTo(dc, xi + r, y0 + b);
      DeleteObject(SelectObject(dc, oldpen));
    }
    void lines(char x1, char y1, char x2, char y2, char x3, char y3)
    {
      int _x1 = char_width * x1 / 8;
      int _y1 = char_height * y1 / 8;
      int _x2 = char_width * x2 / 8;
      int _y2 = char_height * y2 / 8;
      int _x3 = char_width * x3 / 8;
      int _y3 = char_height * y3 / 8;
      if (x2) {
        _x1--; _x2--; _x3--;
      }

      int w = y3 >= 0 ? line_width : 0;
      HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, w, fg));
      MoveToEx(dc, xi + _x1, y0 + _y1, null);
      LineTo(dc, xi + _x2, y0 + _y2);
      if (y3 >= 0) {
        MoveToEx(dc, xi + _x3, y0 + _y3 - 1, null);
        LineTo(dc, xi + _x2, y0 + _y2 - 1);
      }
      DeleteObject(SelectObject(dc, oldpen));
    }
    void trio(char x1, char y1, char x2, char y2, char x3, char y3, bool chord)
    {
      int _x1 = char_width * x1 / 8;
      int _y1 = char_height * y1 / 8;
      int _x2 = char_width * x2 / 8;
      int _y2 = char_height * y2 / 8;
      int _x3 = char_width * x3 / 8;
      int _y3 = char_height * y3 / 8;
      if (x1) _x1--;
      if (x2) _x2--;
      if (x3) _x3--;
      if (y2 == 8) _y2--;
      _y3--;
      if (chord && !x1) {
        _x1++; _x2++; _x3++;
      }

      HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
      HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(fg));
      if (chord) {
        if (x1)
          Chord(dc, xi, y0 + _y1, xi + 2 * _x1, y0 + _y3,
                    xi + _x1, y0 + _y1, xi + _x3, y0 + _y3);
        else
          Chord(dc, xi - _x2, y0 + _y1, xi + x2, y0 + _y3,
                    xi + _x3, y0 + _y3, xi + _x1, y0 + _y1);
      }
      else
        Polygon(dc, (POINT[]){{xi + _x1, y0 + _y1},
                              {xi + _x2, y0 + _y2},
                              {xi + _x3, y0 + _y3}}, 3);
      DeleteObject(SelectObject(dc, oldbrush));
      DeleteObject(SelectObject(dc, oldpen));
    }
    void triangle(char x1, char y1, char x2, char y2, char x3, char y3)
    {
      trio(x1, y1, x2, y2, x3, y3, false);
    }
    void trichord(char x1, char y1, char x2, char y2, char x3, char y3)
    {
      trio(x1, y1, x2, y2, x3, y3, true);
    }
    void rectdraw(char l, char t, char r, char b, char sol, colour c)
    {
      // solid 0b1111 top right bottom left
      int cl = char_width * l;
      int ct = char_height * t;
      int cr = char_width * r;
      int cb = char_height * b;
      int dl = cl % 8;
      int dt = ct % 8;
      int dr = cr % 8;
      int db = cb % 8;
      cl /= 8;
      ct /= 8;
      cr /= 8;
      cb /= 8;
      //printf("25%02X <%d%%%d %d%%%d %d%%%d %d%%%d\n", graph, cl, dl, ct, dt, cr, dr, cb, db);
      int cl_ = cl;
      int ct_ = ct;
      int cr_ = cr;
      int cb_ = cb;
      if (dl) {
        if (sol & 0x1)
          dl = 0;
        else
          cl_++;
      }
      if (dt) {
        if (sol & 0x8)
          dt = 0;
        else
          ct_++;
      }
      if (dr) {
        if (sol & 0x4) {
          dr = 0;
          cr_++;
        }
      }
      if (db) {
        if (sol & 0x2) {
          db = 0;
          cb_++;
        }
      }
      //printf("25%02X >%d%%%d %d%%%d %d%%%d %d%%%d\n", graph, cl, dl, ct, dt, cr, dr, cb, db);
      //printf("Rect %d %d %d %d\n", xi + cl_, y0 + ct_, xi + cr_, y0 + cb_);
      HBRUSH br = CreateSolidBrush(c);
      FillRect(dc, &(RECT){xi + cl_, y0 + ct_, xi + cr_, y0 + cb_}, br);
      DeleteObject(br);
      if (dl)
        linedraw(cl, ct, cl, cb, colmix(8 - dl));
      if (dt)
        linedraw(cl, ct, cr, ct, colmix(8 - dt));
      if (dr)
        linedraw(cr, ct, cr, cb, colmix(dr));
      if (db)
        linedraw(cl, cb, cr, cb, colmix(db));
    }
    inline void rect(char l, char t, char r, char b)
    {
      rectdraw(l, t, r, b, 0, fg);
    }
    inline void rectsolcol(char l, char t, char r, char b, char mix)
    {
      rectdraw(l, t, r, b, 0xF, colmix(mix));
    }
    inline void rectsolid(char l, char t, char r, char b, char sol)
    {
      rectdraw(l, t, r, b, sol, fg);
    }

    for (int i = 0; i < ulen; i++) {
      if (powerline && origtext) switch (origtext[i]) {
        when 0xE0B0:
          triangle(0, 0, 8, 4, 0, 8);
        when 0xE0B1:
          lines(0, 0, 8, 4, 0, 8);
        when 0xE0B2:
          triangle(8, 0, 0, 4, 8, 8);
        when 0xE0B3:
          lines(8, 0, 0, 4, 8, 8);
        when 0xE0B4:
          trichord(0, 0, 8, 4, 0, 8);
        when 0xE0B6:
          trichord(8, 0, 0, 4, 8, 8);
        when 0xE0B8:
          triangle(0, 0, 0, 8, 8, 8);
        when 0xE0B9:
          lines(0, 0, 8, 8, -1, -1);
        when 0xE0BA:
          triangle(8, 0, 8, 8, 0, 8);
        when 0xE0BB:
          lines(8, 0, 0, 8, -1, -1);
        when 0xE0BC:
          triangle(0, 0, 8, 0, 0, 8);
        when 0xE0BD:
          lines(8, 0, 0, 8, -1, -1);
        when 0xE0BE:
          triangle(0, 0, 8, 0, 8, 8);
        when 0xE0BF:
          lines(0, 0, 8, 8, -1, -1);
      } else switch (graph) {
        // ▀▁▂▃▄▅▆▇█▉▊▋▌▍▎▏▐░▒▓▔▕▖▗▘▙▚▛▜▝▞▟
        when 0x80: rect(0, 0, 8, 4); // UPPER HALF BLOCK
        when 0x81 ... 0x88: rect(0, 0x88 - graph, 8, 8); // LOWER EIGHTHS
        when 0x89 ... 0x8F: rect(0, 0, 0x90 - graph, 8); // LEFT EIGHTHS
        when 0x90: rect(4, 0, 8, 8); // RIGHT HALF BLOCK
        when 0x91: rectsolcol(0, 0, 8, 8, 2); // ░ LIGHT SHADE
        when 0x92: rectsolcol(0, 0, 8, 8, 3); // ▒ MEDIUM SHADE
        when 0x93: rectsolcol(0, 0, 8, 8, 5); // ▓ DARK SHADE
        when 0x94: rect(0, 0, 8, 1); // UPPER ONE EIGHTH BLOCK
        when 0x95: rect(7, 0, 8, 8); // RIGHT ONE EIGHTH BLOCK
        when 0x96: rect(0, 4, 4, 8);
        when 0x97: rect(4, 4, 8, 8);
        when 0x98: rect(0, 0, 4, 4);
        // solid 0b1111 top right bottom left
        when 0x99: rectsolid(0, 4, 4, 8, 0xF);
                   rectsolid(0, 0, 4, 4, 0xB);
                   rectsolid(4, 4, 8, 8, 0x7);
        when 0x9A: rect(0, 0, 4, 4); rect(4, 4, 8, 8);
        when 0x9B: rectsolid(0, 0, 4, 4, 0xF);
                   rectsolid(4, 0, 8, 4, 0xD);
                   rectsolid(0, 4, 4, 8, 0xB);
        when 0x9C: rectsolid(4, 0, 8, 4, 0xF);
                   rectsolid(0, 0, 4, 4, 0xD);
                   rectsolid(4, 4, 8, 8, 0xE);
        when 0x9D: rect(4, 0, 8, 4);
        when 0x9E: rect(4, 0, 8, 4); rect(0, 4, 4, 8);
        when 0x9F: rectsolid(4, 4, 8, 8, 0xF);
                   rectsolid(4, 0, 8, 4, 0xE);
                   rectsolid(0, 4, 4, 8, 0x7);
      }

      xi += char_width;
    }
  }
  else if (graph >> 4) {  // VT100/VT52 horizontal "scanlines"
    int parts = graph_vt52 ? 8 : 5;
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
    int yoff = (cell_height - line_width) * (graph >> 4) / parts;
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

 /* Strikeout */
  if ((attr.attr & ATTR_STRIKEOUT)
      && (cfg.underl_manual || cfg.underl_colour != (colour)-1
          || (attr.attr & ATTR_ULCOLOUR)
         )
     )
  {
    int soff = (ff->descent + (ff->row_spacing / 2)) * 2 / 3;
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, ul));
    for (int l = 0; l < line_width; l++) {
      MoveToEx(dc, x, y + soff + l, null);
      LineTo(dc, x + ulen * char_width, y + soff + l);
    }
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }

  if (origtext)
    free(origtext);

  show_curchar_info('w');
  if (has_cursor) {
    colour _cc = cursor_colour;
    if (layer)
      _cc = ((_cc & 0xFEFEFEFE) >> 1) + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
#if defined(debug_cursor) && debug_cursor > 1
    printf("painting cursor_type '%c' cursor_on %d\n", "?b_l"[term_cursor_type()+1], term.cursor_on);
#endif
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, _cc));
    switch (term_cursor_type()) {
      when CUR_BLOCK:
        if (attr.attr & TATTR_PASCURS) {
          HBRUSH oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
          Rectangle(dc, x, y, x + char_width, y + cell_height);
          SelectObject(dc, oldbrush);
        }
      when CUR_BOX: {
        HBRUSH oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, x, y, x + char_width, y + cell_height);
        SelectObject(dc, oldbrush);
      }
      when CUR_LINE: {
        int caret_width = 1;
        SystemParametersInfo(SPI_GETCARETWIDTH, 0, &caret_width, 0);
        // limit line cursor width by char_width? rather by line_width (#1101)
        if (caret_width > line_width)
          caret_width = line_width;
        int xx = x;
        if (attr.attr & TATTR_RIGHTCURS)
          xx += char_width - caret_width;
        if (attr.attr & TATTR_ACTCURS) {
#ifdef cursor_painted_with_rectangle
          // this would add an additional line, vanishing again but 
          // leaving a pixel artefact, under some utterly weird interference 
          // with output of certain characters (mintty/wsltty#255)
          HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(_cc));
          Rectangle(dc, xx, y, xx + caret_width, y + cell_height);
          DeleteObject(SelectObject(dc, oldbrush));
#else
          HBRUSH br = CreateSolidBrush(_cc);
          FillRect(dc, &(RECT){xx, y, xx + caret_width, y + cell_height}, br);
          DeleteObject(br);
#endif
        }
        else if (attr.attr & TATTR_PASCURS) {
          for (int dy = 0; dy < cell_height; dy += 2)
            Polyline(
              dc, (POINT[]){{xx, y + dy}, {xx + caret_width, y + dy}}, 2);
        }
      }
      when CUR_UNDERSCORE: {
        int yy = yt + min(ff->descent, cell_height - 2);
        yy += ff->row_spacing * 3 / 8;
        if (lattr >= LATTR_TOP) {
          yy += ff->row_spacing / 2;
          if (lattr == LATTR_BOT)
            yy += cell_height;
        }
        if (attr.attr & TATTR_ACTCURS) {
	  /* cursor size CSI ? N c
	     from linux console https://linuxgazette.net/137/anonymous.html
		0   default
		1   invisible
		2   underscore
		3   lower_third
		4   lower_half
		5   two_thirds
		6   full block
          */
          int up = 0;
          switch (term.cursor_size) {
            when 1: up = -2;
            when 2: up = line_width - 1;
            when 3: up = cell_height / 3 - 1;
            when 4: up = cell_height / 2;
            when 5: up = cell_height * 2 / 3;
            when 6: up = cell_height - 2;
          }
          if (up) {
            int yct = max(yy - up, yt);
            HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(_cc));
            Rectangle(dc, x, yct, x + char_width, yy + 2);
            DeleteObject(SelectObject(dc, oldbrush));
          }
          else
            Rectangle(dc, x, yy - up, x + char_width, yy + 2);
        }
        else if (attr.attr & TATTR_PASCURS) {
          for (int dx = 0; dx < char_width; dx += 2) {
            SetPixel(dc, x + dx, yy, _cc);
            SetPixel(dc, x + dx, yy + 1, _cc);
          }
        }
      }
    }
    DeleteObject(SelectObject(dc, oldpen));
  }

  _return:

  if (bloom && coord_transformed_bloom) {
    bloom--;
    SetWorldTransform(dc, &old_xform_bloom);
    fg = fg0;
    SetTextColor(dc, fg);
    if (!bloom)
      SelectObject(dc, ff->fonts[nfont]);
    goto draw;
  }

  if (layer) {
    layer--;
    yt = yt0;
    xt = xt0;
    y = y0;
    x = x0;
    if (!layer) {
      xt -= line_width;
      x -= line_width;
      fg = fg0;
      ul = ul0;
      SetTextColor(dc, fg);
    }
    yt += line_width;
    y += line_width;
    underlaid = true;
    goto draw;
  }

  if (coord_transformed)
    SetWorldTransform(dc, &old_xform);
}


static HFONT
font4(struct fontfam * ff, cattrflags attr)
{
  bool bold = (ff->bold_mode == BOLD_FONT) && (attr & ATTR_BOLD);
  bool italic = attr & ATTR_ITALIC;
  int font4index = (bold ? FONT_BOLD : 0) | (italic ? FONT_ITALIC : 0);
  HFONT f = ff->fonts[font4index];
  if (!f && italic) {
    if (!ff->fonts[FONT_BOLD]) {
      font4index &= ~FONT_BOLD;
      f = ff->fonts[font4index];
    }
    if (!f) {
      another_font(ff, font4index);
      f = ff->fonts[font4index];
    }
  }
  if (!f)
    f = ff->fonts[FONT_NORMAL];
  return f;
}

/* Check availability of characters in the current font.
 * Zeroes each of the characters in the input array that isn't available.
 */
void
win_check_glyphs(wchar *wcs, uint num, cattrflags attr)
{
  int findex = (attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  if (findex > 10)
    findex = 0;

  struct fontfam * ff = &fontfamilies[findex];

  HFONT f = font4(ff, attr);

  HDC dc = GetDC(wnd);
  SelectObject(dc, f);
  ushort glyphs[num];
  GetGlyphIndicesW(dc, wcs, num, glyphs, true);

  // recheck for characters affected by FontChoice
  for (uint i = 0; i < num; i++) {
    uchar cf = scriptfont(wcs[i]);
#ifdef debug_scriptfonts
    if (wcs[i] && cf)
      printf("scriptfont %04X: %d\n", wcs[i], cf);
#endif
    if (cf && cf <= 10) {
      struct fontfam * ff = &fontfamilies[cf];
      f = font4(ff, attr);
      SelectObject(dc, f);
      GetGlyphIndicesW(dc, &wcs[i], 1, &glyphs[i], true);
    }
  }

  for (uint i = 0; i < num; i++) {
    if (glyphs[i] == 0xFFFF || glyphs[i] == 0x1F)
      wcs[i] = 0;
  }

#ifdef check_font_ranges
#warning this does not tell us whether a glyph is shown
  bool bold = (ff->bold_mode == BOLD_FONT) && (attr & ATTR_BOLD);
  bool italic = attr & ATTR_ITALIC;
  int font4index = (bold ? FONT_BOLD : 0) | (italic ? FONT_ITALIC : 0);
  GLYPHSET * gs = win_font_ranges(dc, ff, font4index);
  if (gs)
    for (uint i = 0; i < num; i++) {
      if (!glyph_in(wcs[i], gs))
        wcs[i] = 0;
    }
#endif

  ReleaseDC(wnd, dc);
}

#define dont_debug_win_char_width 2

#ifdef debug_win_char_width
int
win_char_width(xchar c, cattrflags attr)
{
#define win_char_width xwin_char_width
int win_char_width(xchar, cattrflags);
  ulong t = mtime();
  int w = win_char_width(c, attr);
  if (c >= 0x80)
    printf(" [%ld:%ld] win_char_width(%04X) -> %d\n", t, mtime() - t, c, w);
  return w;
}
#endif

/* This function gets the actual width of a character in the normal font.
   Usage:
   * determine whether to trim an ambiguous wide character 
     (of a CJK ambiguous-wide font such as BatangChe) to normal width 
     if desired.
   * also whether to expand a normal width character if expected wide
 */
int
win_char_width(xchar c, cattrflags attr)
{
  // NOTE: if wintext.c is compiled with optimization (-O1 or higher), 
  // and win_char_width is called for a non-BMP character (>= 0x10000), 
  // (unless inhibited by calls to GetCharABCWidths* below)
  // a mysterious delay occurs, if tracing with timestamps apparently 
  // *before* invocation of win_char_width (unless printf gets delayed...)

  int findex = (attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
  if (findex > 10)
    findex = 0;
  struct fontfam * ff = &fontfamilies[findex];
#ifdef debug_win_char_width
  if (c > 0xFF)
    printf("win_char_width(%04X) font %d\n", c, findex);
#endif

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

  HFONT f = font4(ff, attr);

  HDC dc = GetDC(wnd);
#ifdef debug_win_char_width
  bool ok0 = !!
#endif
  SelectObject(dc, f);
#ifdef debug_win_char_width
  if (c == 0x2001)
    win_char_width(0x5555, attr);
  if (!ok0)
    printf(" wdth %04X failed (dc %p)\n", c, dc);
  else if (c > '~' || c == 'A') {
    int cw = 0;
    BOOL ok1 = GetCharWidth32W(dc, c, c, &cw);  // "not on TrueType"
    float cwf = 0.0;
    BOOL ok2 = GetCharWidthFloatW(dc, c, c, &cwf);
    ABC abc; memset(&abc, 0, sizeof abc);
    // NOTE: with these 2 calls of GetCharABCWidths*, 
    // the mysterious delay for non-BMP characters does not occur, 
    // again mysteriously
    BOOL ok3 = GetCharABCWidthsW(dc, c, c, &abc);  // only on TrueType
    ABCFLOAT abcf; memset(&abcf, 0, sizeof abcf);
    BOOL ok4 = GetCharABCWidthsFloatW(dc, c, c, &abcf);
    printf(" w %04X [cell %d] - 32 %d %d - flt %d %.3f - abc %d %d %d %d - abcflt %d %4.1f %4.1f %4.1f\n", 
           c, cell_width, 
           ok1, cw, ok2, cwf, 
           ok3, abc.abcA, abc.abcB, abc.abcC, 
           ok4, abcf.abcfA, abcf.abcfB, abcf.abcfC);
  }
#endif

  int ibuf = 0;

#ifdef use_getcharwidth
#warning avoid buggy GetCharWidth*
  if (c < 0x10000) {
    bool ok = GetCharWidth32W(dc, c, c, &ibuf);
#ifdef debug_win_char_width
    printf(" getcharwidth32 %04X %dpx(/cell %dpx)\n", c, ibuf, cell_width);
#endif
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
      printf(" enquired %04X %dpx/cell %dpx\n", c, ibuf, cell_width);
#endif
      ReleaseDC(wnd, dc);
      //printf(" win_char_width %04X -> %d\n", c, ibuf);
      return ibuf;
    }
  }
#endif

#ifdef measure_width

#define dont_debug_rendering

  int act_char_width(xchar wc)
  {
# ifdef debug_rendering
# include <time.h>
    struct timespec tim;
    clock_gettime(CLOCK_MONOTONIC, &tim);
    ulong now = tim.tv_sec * (long)1000000000 + tim.tv_nsec;
# endif
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
    wchar wc2[2];
    if (wc < 0x10000) {
      *wc2 = wc;
      text_out_start(wid_dc, wc2, 1, &dx);
      text_out(wid_dc, 0, 0, ETO_OPAQUE, null, wc2, 1, &dx);
    }
    else {
      wc2[0] = high_surrogate(wc);
      wc2[1] = low_surrogate(wc);
      text_out_start(wid_dc, wc2, 2, &dx);
      text_out(wid_dc, 0, 0, ETO_OPAQUE, null, wc2, 2, &dx);
    }
    text_out_end();

    int wid = 0;

//#define debug_win_char_width 2

#ifdef use_GetPixel

# if defined(debug_win_char_width) && debug_win_char_width > 1
    for (int y = 0; y < cell_height; y++) {
      printf(" %2d|", y);
      for (int x = 0; x < cell_width * 2; x++) {
        COLORREF c = GetPixel(wid_dc, x, y);
        printf("%c", c != RGB(0, 0, 0) ? '*' : ' ');
      }
      printf("|\n");
    }
# endif
# ifdef heuristic_sparse_width_checking
    for (int x = cell_width * 2 - 1; !wid && x >= cell_width; x -= 2)
      for (int y = 0; y < cell_height / 2; y++) {
        COLORREF c = GetPixel(wid_dc, x, cell_height / 2 + y);
        if (c != RGB(0, 0, 0)) {
          wid = x + 1;
          break;
        }
        c = GetPixel(wid_dc, x, cell_height / 2 - y);
        if (c != RGB(0, 0, 0)) {
          wid = x + 1;
          break;
        }
      }
# else
    for (int x = cell_width * 2 - 1; !wid && x >= 0; x--)
      for (int y = 0; y < cell_height; y++) {
        COLORREF c = GetPixel(wid_dc, x, y);
        if (c != RGB(0, 0, 0)) {
          wid = x + 1;
          break;
        }
      }
# endif
    SelectObject(wid_dc, wid_oldbm);

#else // use_GetPixel

    SelectObject(wid_dc, wid_oldbm);

# ifdef test_preload_bitmap_info
    BITMAP bm0;
    GetObject(wid_bm, sizeof(BITMAP), &bm0);
    //assuming:
    //bm0.bmWidthBytes == bm0.bmWidth * 4 == cell_width * 8
    //bm0.bmBitsPixel == 32
# endif
    BITMAPINFO bmi;
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
# ifdef test_precheck_bitmap
    int ok = GetDIBits(wid_dc, wid_bm, 0, cell_height, 0, &bmi, DIB_RGB_COLORS);
    printf("DI %d %d pl %d bt/px %d comp %d size %d\n",
           bmi.bmiHeader.biWidth, bmi.bmiHeader.biHeight,
           bmi.bmiHeader.biPlanes, bmi.bmiHeader.biBitCount,
           bmi.bmiHeader.biCompression, bmi.bmiHeader.biSizeImage);
    //assuming:
    //bmi.bmiHeader.biBitCount == 32
    //bmi.bmiHeader.biSizeImage == biWidth * biHeight * 4
# endif
    bmi.bmiHeader.biWidth = cell_width * 2;
    bmi.bmiHeader.biHeight = -cell_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD * pixels = newn(DWORD, cell_width * 2 * cell_height);
    //int scanlines =
    GetDIBits(wid_dc, wid_bm, 0, cell_height, pixels, &bmi, DIB_RGB_COLORS);

# if defined(debug_win_char_width) && debug_win_char_width > 1
    for (int y = 0; y < cell_height; y++) {
      printf(" %2d|", y);
      for (int x = 0; x < cell_width * 2; x++) {
        COLORREF c = pixels[y * cell_width * 2 + x];
        printf("%c", c != RGB(0, 0, 0) ? '*' : ' ');
      }
      printf("|\n");
    }
# endif
    for (int x = cell_width * 2 - 1; !wid && x >= 0; x--)
      for (int y = 0; y < cell_height; y++) {
        COLORREF c = pixels[y * cell_width * 2 + x];
        if (c != RGB(0, 0, 0)) {
          wid = x + 1;
          break;
        }
      }

    free(pixels);

#endif // use_GetPixel

    DeleteObject(wid_bm);
    DeleteDC(wid_dc);

# ifdef debug_rendering
    clock_gettime(CLOCK_MONOTONIC, &tim);
    ulong then = tim.tv_sec * (long)1000000000 + tim.tv_nsec;
    printf("rendered %05X %s (t %ld) width %d -> %d\n", wc, attr & TATTR_WIDE ? "wide" : "narr", then - now, wid, wid > cell_width ? 2 : 1);
# endif

    return wid;
  }

  if ((c >= 0x2160 && c <= 0x2179)   // Roman Numerals
     )
  {
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

  if ((c >= 0x3000 && c <= 0x303F)   // CJK Symbols and Punctuation

   || (c >= 0x01C4 && c <= 0x01CC)   // double letters
   || (c >= 0x01F1 && c <= 0x01F3)   // double letters

   || (c >= 0x2460 && c <= 0x24FF)   // Enclosed Alphanumerics, to cover:
   //|| (c >= 0x249C && c <= 0x24E9)   // parenthesized/circled letters

   //|| (c >= 0x2600 && c <= 0x27BF)   // Miscellaneous Symbols, Dingbats
   || c == 0x26AC

   || (c >= 0x3248 && c <= 0x324F)   // circled CJK Numbers
   || (c >= 0x1F100 && c <= 0x1F1FF) // Enclosed Alphanumeric Supplement

   || c == 0x2139                    // Letterlike: Information Source
   || (c >= 0x2180 && c <= 0x2182)   // Number Forms: combined Roman Numerals
   || (c >= 0x2187 && c <= 0x2188)   // Number Forms: combined Roman Numerals

   || (c >= 0xE000 && c < 0xF900)    // Private Use Area

   || (// check all non-letters with some exceptions
       bidi_class(c) != L               // indicates not a letter
      &&  // do not check these non-letters:
       !(  (c >= 0x2500 && c <= 0x2588)  // Box Drawing, Block Elements
        || (c >= 0x2591 && c <= 0x2594)  // Block Elements
        || (c >= 0x2160 && c <= 0x2179)  // Roman Numerals
        //|| wcschr (W("‐‑‘’‚‛“”„‟‹›"), c) // #712 workaround; now caching
        )
      )
     )
  {
    // look up c in charpropcache
    struct charpropcache * cpfound = 0;

    bool bold = (ff->bold_mode == BOLD_FONT) && (attr & ATTR_BOLD);
    bool italic = attr & ATTR_ITALIC;
    int font4index = (bold ? FONT_BOLD : 0) | (italic ? FONT_ITALIC : 0);

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
      printf(" measured %04X %dpx cell %dpx width %d\n", c, mbuf, cell_width, width);
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
    //printf(" win_char_width %04X -> %d\n", c, width);
    return width;
  }

#endif // measure_width

  ReleaseDC(wnd, dc);
  //printf(" win_char_width %04X -> %d\n", c, ibuf);
  return ibuf;
}

#define dont_debug_win_combine

/* Try to combine a base and combining character into a precomposed one.
 * Returns 0 if unsuccessful.
 */
wchar
win_combine_chars(wchar c, wchar cc, cattrflags attr)
{
  wchar cs[2];
  int len = FoldStringW(MAP_PRECOMPOSED, (wchar[]){c, cc}, 2, cs, 2);
  if (len == 1) {  // check whether the combined glyph exists
    int findex = (attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
    if (findex > 10)
      findex = 0;
    struct fontfam * ff = &fontfamilies[findex];

    HFONT f = font4(ff, attr);

    ushort glyph;
    HDC dc = GetDC(wnd);
    SelectObject(dc, f);
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

  bool changed_something = false;
  void cc(colour_i i, colour c)
  {
    if (c != colours[i]) {
      colours[i] = c;
      changed_something = true;
    }
  }

  if (c == (colour)-1) {
    // ... reset to default ...
    if (i == BOLD_COLOUR_I) {
      cc(BOLD_COLOUR_I, cfg.bold_colour);
    }
    else if (i == BLINK_COLOUR_I) {
      cc(BLINK_COLOUR_I, cfg.blink_colour);
    }
    else if (i == BOLD_FG_COLOUR_I) {
      bold_colour_selected = false;
      if (cfg.bold_colour != (colour)-1)
        cc(BOLD_FG_COLOUR_I, cfg.bold_colour);
      else
        cc(BOLD_FG_COLOUR_I, brighten(colours[FG_COLOUR_I], colours[BG_COLOUR_I], true));
    }
    else if (i == FG_COLOUR_I)
      cc(i, cfg.fg_colour);
    else if (i == BG_COLOUR_I)
      cc(i, cfg.bg_colour);
    else if (i == CURSOR_COLOUR_I) {
      cc(i, cfg.cursor_colour);
      if (cfg.ime_cursor_colour != DEFAULT_COLOUR)
        cc(IME_CURSOR_COLOUR_I, cfg.ime_cursor_colour);
      //printf("ime_cc set -1 %06X\n", cfg.ime_cursor_colour);
    }
    else if (i == SEL_COLOUR_I)
      cc(i, cfg.sel_bg_colour);
    else if (i == SEL_TEXT_COLOUR_I)
      cc(i, cfg.sel_fg_colour);
    else if (i == TEK_FG_COLOUR_I)
      cc(i, cfg.tek_fg_colour);
    else if (i == TEK_BG_COLOUR_I)
      cc(i, cfg.tek_bg_colour);
    else if (i == TEK_CURSOR_COLOUR_I)
      cc(i, cfg.tek_cursor_colour);
  }
  else {
    cc(i, c);
    if (i < 16)
      cc(i + ANSI0, c);
#ifdef debug_brighten
    printf("colours[%d] = %06X\n", i, c);
#endif

    switch (i) {
      when FG_COLOUR_I:
        // should we make this conditional, 
        // unless bold colour has been set explicitly?
        if (!bold_colour_selected) {
          if (cfg.bold_colour != (colour)-1)
            cc(BOLD_FG_COLOUR_I, cfg.bold_colour);
          else {
            cc(BOLD_FG_COLOUR_I, brighten(c, colours[BG_COLOUR_I], true));
            // renew this too as brighten() may refer to contrast colour:
            cc(BOLD_BG_COLOUR_I, brighten(colours[BG_COLOUR_I], colours[FG_COLOUR_I], true));
          }
        }
      when BOLD_FG_COLOUR_I:
        bold_colour_selected = true;
      when BG_COLOUR_I:
        if (!bold_colour_selected) {
          if (cfg.bold_colour != (colour)-1)
            cc(BOLD_FG_COLOUR_I, cfg.bold_colour);
          else {
            cc(BOLD_BG_COLOUR_I, brighten(c, colours[FG_COLOUR_I], true));
            // renew this too as brighten() may refer to contrast colour:
            cc(BOLD_FG_COLOUR_I, brighten(colours[FG_COLOUR_I], colours[BG_COLOUR_I], true));
          }
        }
      when CURSOR_COLOUR_I: {
        // Set the colour of text under the cursor to whichever of foreground
        // and background colour is further away from the cursor colour.
        colour fg = colours[FG_COLOUR_I], bg = colours[BG_COLOUR_I];
        colour _cc = colour_dist(c, fg) > colour_dist(c, bg) ? fg : bg;
        cc(CURSOR_TEXT_COLOUR_I, _cc);
        if (cfg.ime_cursor_colour != DEFAULT_COLOUR) {
          // effective IME cursor colour : configured IME cursor colour
          // = effective cursor colour : configured cursor colour
          // resp.
          // effective IME cursor colour : effective cursor colour
          // = configured IME cursor colour : configured cursor colour
          uint r = (uint)red(_cc) * (uint)red(cfg.ime_cursor_colour);
          if (red(cfg.cursor_colour))
            r /= red(cfg.cursor_colour);
          r = max(r, 255);
          uint g = (uint)green(_cc) * (uint)green(cfg.ime_cursor_colour);
          if (green(cfg.cursor_colour))
            g /= green(cfg.cursor_colour);
          g = max(r, 255);
          uint b = (uint)blue(_cc) * (uint)blue(cfg.ime_cursor_colour);
          if (blue(cfg.cursor_colour))
            b /= blue(cfg.cursor_colour);
          b = max(r, 255);
          c = RGB(r, g, b);
        }
        cc(IME_CURSOR_COLOUR_I, c);
        //printf("ime_cc set c %06X\n", c);
      }
      otherwise:
        break;
    }
  }

  // Redraw everything.
  if (changed_something)
    win_invalidate_all(false);
}

colour
win_get_colour(colour_i i)
{
  if (term.rvideo && CCL_DEFAULT(i))
    return colours[i ^ 2];  // [BOLD]_FG_COLOUR_I  <-->  [BOLD]_BG_COLOUR_I
  return i < COLOUR_NUM ? colours[i] : 0;
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
  if (cfg.ime_cursor_colour != DEFAULT_COLOUR) {
    win_set_colour(IME_CURSOR_COLOUR_I, cfg.ime_cursor_colour);
    //printf("ime_cc reset %06X\n", cfg.ime_cursor_colour);
  }
  win_set_colour(SEL_COLOUR_I, cfg.sel_bg_colour);
  win_set_colour(SEL_TEXT_COLOUR_I, cfg.sel_fg_colour);
  // attribute colours
  win_set_colour(BOLD_COLOUR_I, (colour)-1);
  win_set_colour(BLINK_COLOUR_I, (colour)-1);
#if defined(debug_bold) || defined(debug_brighten)
  string ci[] = {"FG_COLOUR_I", "BOLD_FG_COLOUR_I", "BG_COLOUR_I", "BOLD_BG_COLOUR_I", "CURSOR_TEXT_COLOUR_I", "CURSOR_COLOUR_I", "IME_CURSOR_COLOUR_I", "SEL_COLOUR_I", "SEL_TEXT_COLOUR_I", "BOLD_COLOUR_I"};
  for (int i = FG_COLOUR_I; i < COLOUR_NUM; i++)
    if (colours[i] == (colour)-1)
      printf("colour %d ------ [%s]\n", i, ci[i - FG_COLOUR_I]);
    else
      printf("colour %d %06X [%s]\n", i, (int)colours[i], ci[i - FG_COLOUR_I]);
#endif
  win_set_colour(TEK_FG_COLOUR_I, cfg.tek_fg_colour);
  win_set_colour(TEK_BG_COLOUR_I, cfg.tek_bg_colour);
  win_set_colour(TEK_CURSOR_COLOUR_I, cfg.tek_cursor_colour);
}


#define dont_debug_padding_background

void
win_paint(void)
{
  PAINTSTRUCT p;
  dc = BeginPaint(wnd, &p);

  // better invalidate more than less; limited to text area in term_invalidate
  term_invalidate(
    (p.rcPaint.left - PADDING) / cell_width,
    (p.rcPaint.top - PADDING - OFFSET) / cell_height,
    (p.rcPaint.right - PADDING - 1) / cell_width,
    (p.rcPaint.bottom - PADDING - OFFSET - 1) / cell_height
  );

  //if (kb_trace) printf("[%ld] win_paint state %d (idl/blk/pnd)\n", mtime(), update_state);
  if (update_state != UPDATE_PENDING) {
    if (tek_mode)
      tek_paint();
    else {
      term_paint();
      winimgs_paint();
    }
  }

  if (// check whether no background was configured and successfully loaded
      !bgbrush_bmp &&
#if CYGWIN_VERSION_API_MINOR >= 74
      !bgbrush_img &&
#endif
      // check whether we need to refresh padding background
      (p.fErase
       || p.rcPaint.left < PADDING
       || p.rcPaint.top < OFFSET + PADDING
       || p.rcPaint.right >= PADDING + cell_width * term.cols
       || p.rcPaint.bottom >= OFFSET + PADDING + cell_height * term.rows
      )
     )
  {
    /* Notes:
       * Do we actually need this stuff? We paint the background with
         each win_text chunk anyway, except for the padding border,
         which could however be touched e.g. by Sixel images?
       * With a texture/image background, we could try to paint that here 
         (invoked on WM_PAINT) or on WM_ERASEBKGND, but these messages are 
         not received sufficiently often, e.g. not when scrolling.
       * So let's keep finer control and paint background with text chunks 
         but not modify the established behaviour if there is no background.
     */
    colour bg_colour = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
#ifdef debug_padding_background
    // visualize background for testing
    bg_colour = RGB(222, 0, 0);
#endif
    HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(bg_colour));
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, bg_colour));

    // unclear purpose
    IntersectClipRect(dc, p.rcPaint.left, p.rcPaint.top,
                          p.rcPaint.right, p.rcPaint.bottom);

    // mask inner area not to pad with background
    ExcludeClipRect(dc, PADDING,
                        OFFSET + PADDING,
                        PADDING + cell_width * term.cols,
                        OFFSET + PADDING + cell_height * term.rows);

    // fill outer padding area with background
    int sy = win_search_visible() ? SEARCHBAR_HEIGHT : 0;
    Rectangle(dc, p.rcPaint.left, max(p.rcPaint.top, OFFSET),
                  p.rcPaint.right, p.rcPaint.bottom - sy);

    DeleteObject(SelectObject(dc, oldbrush));
    DeleteObject(SelectObject(dc, oldpen));
#ifdef debug_padding_background
    // show visualized background for testing
    usleep(900000);
#endif
  }

  EndPaint(wnd, &p);
}

