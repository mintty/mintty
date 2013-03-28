// wintext.c (part of mintty)
// Copyright 2008-12 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "minibidi.h"

#include <winnls.h>

enum {
  FONT_NORMAL     = 0,
  FONT_BOLD       = 1,
  FONT_UNDERLINE  = 2,
  FONT_BOLDUND    = 3,
  FONT_WIDE       = 0x04,
  FONT_HIGH       = 0x08,
  FONT_NARROW     = 0x10,
  FONT_MAXNO      = 0x1F,
  FONT_SHIFT      = 5
};

LOGFONT lfont;
static HFONT fonts[FONT_MAXNO];
static bool fontflag[FONT_MAXNO];

enum {LDRAW_CHAR_NUM = 31, LDRAW_CHAR_TRIES = 4};

// VT100 linedraw character mappings for current font.
wchar win_linedraw_chars[LDRAW_CHAR_NUM];

// Possible linedraw character mappings, in order of decreasing suitability.
// The last resort for each is an ASCII character, which we assume will be
// available in any font. 
static const wchar linedraw_chars[LDRAW_CHAR_NUM][LDRAW_CHAR_TRIES] = {
  {0x25C6, 0x2666, '*'},           // 0x60 '`' Diamond 
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
  {0x23BA, 0x00AF, '-'},           // 0x6F 'o' High horizontal line
  {0x23BB, 0x00AF, '-'},           // 0x70 'p' Medium-high horizontal line
  {0x2500, 0x2015, 0x2014, '-'},   // 0x71 'q' Middle horizontal line
  {0x23BC, '_'},                   // 0x72 'r' Medium-low horizontal line
  {0x23BF, '_'},                   // 0x73 's' Low horizontal line
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

static enum {BOLD_NONE, BOLD_SHADOW, BOLD_FONT} bold_mode;
static enum {UND_LINE, UND_FONT} und_mode;
static int descent;

// Current font size (with any zooming)
static int font_size; 

// Font screen dimensions
int font_width, font_height;
static bool font_dualwidth;

bool font_ambig_wide;

COLORREF colours[COLOUR_NUM];

static colour 
brighten(colour c)
{
  uint r = red(c), g = green(c), b = blue(c);   
  uint s = min(85, 255 - max(max(r, g), b));
  return make_colour(r + s, g + s, b + s);
}

static uint
colour_dist(colour a, colour b)
{
  return
    2 * sqr(red(a) - red(b)) +
    4 * sqr(green(a) - green(b)) +
    1 * sqr(blue(a) - blue(b));
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

static HFONT
create_font(int weight, bool underline)
{
  return 
    CreateFont(
      font_height, 0, 0, 0, weight, false, underline, false,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      get_font_quality(), FIXED_PITCH | FF_DONTCARE,
      cfg.font.name
    );
}
 
/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (poentially) twentyone fonts are done
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
  TEXTMETRIC tm;
  int fontsize[3];
  int i;
  int fw_dontcare, fw_bold;

  font_size = size;

  for (i = 0; i < FONT_MAXNO; i++) {
    if (fonts[i]) {
      DeleteObject(fonts[i]);
      fonts[i] = 0;
    }
    fontflag[i] = 0;
  }

  bold_mode = cfg.bold_as_font ? BOLD_FONT : BOLD_NONE;
  und_mode = UND_FONT;

  if (cfg.font.isbold) {
    fw_dontcare = FW_BOLD;
    fw_bold = FW_HEAVY;
  }
  else {
    fw_dontcare = FW_DONTCARE;
    fw_bold = FW_BOLD;
  }

  HDC dc = GetDC(wnd);
  font_height =
    size > 0 ? -MulDiv(size, GetDeviceCaps(dc, LOGPIXELSY), 72) : size;
  font_width = 0;

  fonts[FONT_NORMAL] = create_font(fw_dontcare, false);

  GetObject(fonts[FONT_NORMAL], sizeof (LOGFONT), &lfont);

  SelectObject(dc, fonts[FONT_NORMAL]);
  GetTextMetrics(dc, &tm);

  font_height = tm.tmHeight + cfg.row_spacing;
  font_width = tm.tmAveCharWidth + cfg.col_spacing;
  font_dualwidth = (tm.tmMaxCharWidth >= tm.tmAveCharWidth * 3 / 2);
  
  // Determine whether ambiguous-width characters are wide in this font */
  float latin_char_width, greek_char_width, line_char_width;
  GetCharWidthFloatW(dc, 0x0041, 0x0041, &latin_char_width);
  GetCharWidthFloatW(dc, 0x03B1, 0x03B1, &greek_char_width);
  GetCharWidthFloatW(dc, 0x2500, 0x2500, &line_char_width);

  font_ambig_wide =
    greek_char_width >= latin_char_width * 1.5 ||
    line_char_width  >= latin_char_width * 1.5;
  
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
  }

  fonts[FONT_UNDERLINE] = create_font(fw_dontcare, true);

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
    int i, gotit;
    COLORREF c;

    und_dc = CreateCompatibleDC(dc);
    und_bm = CreateCompatibleBitmap(dc, font_width, font_height);
    und_oldbm = SelectObject(und_dc, und_bm);
    SelectObject(und_dc, fonts[FONT_UNDERLINE]);
    SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
    SetTextColor(und_dc, RGB(255, 255, 255));
    SetBkColor(und_dc, RGB(0, 0, 0));
    SetBkMode(und_dc, OPAQUE);
    ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, null, " ", 1, null);
    gotit = false;
    for (i = 0; i < font_height; i++) {
      c = GetPixel(und_dc, font_width / 2, i);
      if (c != RGB(0, 0, 0))
        gotit = true;
    }
    SelectObject(und_dc, und_oldbm);
    DeleteObject(und_bm);
    DeleteDC(und_dc);
    if (!gotit) {
      und_mode = UND_LINE;
      DeleteObject(fonts[FONT_UNDERLINE]);
      fonts[FONT_UNDERLINE] = 0;
    }
  }
  
  if (bold_mode == BOLD_FONT)
    fonts[FONT_BOLD] = create_font(fw_bold, false);

  descent = tm.tmAscent + 1;
  if (descent >= font_height)
    descent = font_height - 1;

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
    und_mode = UND_LINE;
    DeleteObject(fonts[FONT_UNDERLINE]);
    fonts[FONT_UNDERLINE] = 0;
  }

  if (bold_mode == BOLD_FONT && fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
    bold_mode = BOLD_SHADOW;
    DeleteObject(fonts[FONT_BOLD]);
    fonts[FONT_BOLD] = 0;
  }
  fontflag[0] = fontflag[1] = fontflag[2] = 1;
}

uint
win_get_font_size(void)
{
  return abs(font_size);
}

void
win_set_font_size(int size)
{
  size = size ? sgn(font_size) * min(size, 72) : cfg.font.size;
  if (size != font_size) {
    win_init_fonts(size);
    win_adapt_term_size();
  }
}

void
win_zoom_font(int zoom)
{
  win_set_font_size(zoom ? max(1, abs(font_size) + zoom) : 0);
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
    (p.rcPaint.left - PADDING) / font_width,
    (p.rcPaint.top - PADDING) / font_height,
    (p.rcPaint.right - PADDING - 1) / font_width,
    (p.rcPaint.bottom - PADDING - 1) / font_height
  );

  if (update_state != UPDATE_PENDING)
    term_paint();

  if (p.fErase || p.rcPaint.left < PADDING ||
      p.rcPaint.top < PADDING ||
      p.rcPaint.right >= PADDING + font_width * term.cols ||
      p.rcPaint.bottom >= PADDING + font_height * term.rows) {
    colour bg_colour = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
    HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(bg_colour));
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, bg_colour));

    IntersectClipRect(dc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
                      p.rcPaint.bottom);

    ExcludeClipRect(dc, PADDING, PADDING,
                    PADDING + font_width * term.cols,
                    PADDING + font_height * term.rows);

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
  term_paint();
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
    int x = term.curs.x * font_width + PADDING;
    int y = (term.curs.y - term.disptop) * font_height + PADDING;
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
  int fw_dontcare, fw_bold;
  int u, w, x;

  if (fontno < 0 || fontno >= FONT_MAXNO || fontflag[fontno])
    return;

  basefont = (fontno & ~(FONT_BOLDUND));
  if (basefont != fontno && !fontflag[basefont])
    another_font(basefont);

  if (cfg.font.isbold) {
    fw_dontcare = FW_BOLD;
    fw_bold = FW_HEAVY;
  }
  else {
    fw_dontcare = FW_DONTCARE;
    fw_bold = FW_BOLD;
  }

  w = fw_dontcare;
  u = false;
  x = font_width;

  if (fontno & FONT_WIDE)
    x *= 2;
  if (fontno & FONT_NARROW)
    x = (x + 1) / 2;
  if (fontno & FONT_BOLD)
    w = fw_bold;
  if (fontno & FONT_UNDERLINE)
    u = true;

  fonts[fontno] =
    CreateFont(font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w, false, u,
               false, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
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


/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void
win_text(int x, int y, wchar *text, int len, uint attr, int lattr)
{
  lattr &= LATTR_MODE;
  int char_width = font_width * (1 + (lattr != LATTR_NORM));

 /* Convert to window coordinates */
  x = x * char_width + PADDING;
  y = y * font_height + PADDING;

  if (attr & ATTR_WIDE)
    char_width *= 2;

 /* Only want the left half of double width lines */
  if (lattr != LATTR_NORM && x * 2 >= term.cols)
    return;

  uint nfont; 
  switch (lattr) {
    when LATTR_NORM: nfont = 0;
    when LATTR_WIDE: nfont = FONT_WIDE;
    otherwise:       nfont = FONT_WIDE + FONT_HIGH;
  }
  if (attr & ATTR_NARROW)
    nfont |= FONT_NARROW;

  if (bold_mode == BOLD_FONT && (attr & ATTR_BOLD))
    nfont |= FONT_BOLD;
  if (und_mode == UND_FONT && (attr & ATTR_UNDER))
    nfont |= FONT_UNDERLINE;
  another_font(nfont);
  
  bool force_manual_underline = false;
  if (!fonts[nfont]) {
    if (nfont & FONT_UNDERLINE)
      force_manual_underline = true;
    // Don't force manual bold, it could be bad news.
    nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
  }
  another_font(nfont);
  if (!fonts[nfont])
    nfont = FONT_NORMAL;

  colour_i fgi = (attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
  colour_i bgi = (attr & ATTR_BGMASK) >> ATTR_BGSHIFT;

  if (term.rvideo) {
    if (fgi >= 256)
      fgi ^= 2;
    if (bgi >= 256)
      bgi ^= 2;
  }
  if (attr & ATTR_BOLD && cfg.bold_as_colour) {
    if (fgi < 8)
      fgi |= 8;
    else if (fgi >= 256 && !cfg.bold_as_font)
      fgi |= 1;
  }
  if (attr & ATTR_BLINK) {
    if (bgi < 8)
      bgi |= 8;
    else if (bgi >= 256)
      bgi |= 1;
  }
  
  colour fg = colours[fgi];
  colour bg = colours[bgi];
  
  if (attr & ATTR_DIM) {
    fg = (fg & 0xFEFEFEFE) >> 1; // Halve the brightness.
    if (!cfg.bold_as_colour || fgi >= 256)
      fg += (bg & 0xFEFEFEFE) >> 1; // Blend with background.
  }
  if (attr & ATTR_REVERSE) {
    colour t = fg; fg = bg; bg = t;
  }
  if (attr & ATTR_INVISIBLE)
    fg = bg;

  bool has_cursor = attr & (TATTR_ACTCURS | TATTR_PASCURS);
  colour cursor_colour = 0;
  
  if (has_cursor) {
    cursor_colour = colours[ime_open ? IME_CURSOR_COLOUR_I : CURSOR_COLOUR_I];
    
    bool too_close = colour_dist(cursor_colour, bg) < 32768;
    
    if (too_close)
      cursor_colour = fg;
    
    if ((attr & TATTR_ACTCURS) && term_cursor_type() == CUR_BLOCK) {
      fg = colours[CURSOR_TEXT_COLOUR_I];
      if (too_close && colour_dist(cursor_colour, fg) < 32768)
        fg = bg;
      bg = cursor_colour;
    }
  }

  SelectObject(dc, fonts[nfont]);
  SetTextColor(dc, fg);
  SetBkColor(dc, bg);
  
 /* Check whether the text has any right-to-left characters */
  bool has_rtl = false;
  for (int i = 0; i < len && !has_rtl; i++)
    has_rtl = is_rtl(text[i]);

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
    
    GetCharacterPlacementW(dc, text, len, 0, &gcpr,
                           FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);
    len = gcpr.nGlyphs;
    eto_options |= ETO_GLYPH_INDEX;
  }

  bool combining = attr & TATTR_COMBINING;
  int width = char_width * (combining ? 1 : len);
  RECT box = {
    .left = x, .top = y,
    .right = min(x + width, font_width * term.cols + PADDING),
    .bottom = y + font_height
  };
  
 /* Array with offsets between neighbouring characters */
  int dxs[len];
  int dx = combining ? 0 : char_width;
  for (int i = 0; i < len; i++)
    dxs[i] = dx;

  int yt = y + cfg.row_spacing - font_height * (lattr == LATTR_BOT);

 /* Finally, draw the text */
  SetBkMode(dc, OPAQUE);
  ExtTextOutW(dc, x, yt, eto_options | ETO_OPAQUE, &box, text, len, dxs);

 /* Shadow bold */
  if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
    SetBkMode(dc, TRANSPARENT);
    ExtTextOutW(dc, x + 1, yt, eto_options, &box, text, len, dxs);
  }

 /* Manual underline */
  if (lattr != LATTR_TOP &&
      (force_manual_underline ||
       (und_mode == UND_LINE && (attr & ATTR_UNDER)))) {
    int dec = (lattr == LATTR_BOT) ? descent * 2 - font_height : descent;
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
    MoveToEx(dc, x, y + dec, null);
    LineTo(dc, x + len * char_width, y + dec);
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }
  
  if (has_cursor) {
    HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, cursor_colour));
    switch(term_cursor_type()) {
      when CUR_BLOCK:
        if (attr & TATTR_PASCURS) {
          HBRUSH oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
          Rectangle(dc, x, y, x + char_width, y + font_height);
          SelectObject(dc, oldbrush);
        }
      when CUR_LINE: {
        int caret_width = 1;
        SystemParametersInfo(SPI_GETCARETWIDTH, 0, &caret_width, 0);
        if (caret_width > char_width)
          caret_width = char_width;
        if (attr & TATTR_RIGHTCURS)
          x += char_width - caret_width;
        if (attr & TATTR_ACTCURS) {
          HBRUSH oldbrush = SelectObject(dc, CreateSolidBrush(cursor_colour));
          Rectangle(dc, x, y, x + caret_width, y + font_height);
          DeleteObject(SelectObject(dc, oldbrush));
        }
        else if (attr & TATTR_PASCURS) {
          for (int dy = 0; dy < font_height; dy += 2)
            Polyline(
              dc, (POINT[]){{x, y + dy}, {x + caret_width, y + dy}}, 2);
        }
      }
      when CUR_UNDERSCORE:
        y += min(descent, font_height - 2);
        if (attr & TATTR_ACTCURS)
          Rectangle(dc, x, y, x + char_width, y + 2);
        else if (attr & TATTR_PASCURS) {
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
  bool bold = (bold_mode == BOLD_FONT) && (term.curs.attr & ATTR_BOLD);
  SelectObject(dc, fonts[bold ? FONT_BOLD : FONT_NORMAL]);
  ushort glyphs[num];
  GetGlyphIndicesW(dc, wcs, num, glyphs, true);
  for (size_t i = 0; i < num; i++) {
    if (glyphs[i] == 0xFFFF || glyphs[i] == 0x1F)
      wcs[i] = 0;
  }
  ReleaseDC(wnd, dc);
}

/* This function gets the actual width of a character in the normal font.
 */
int
win_char_width(xchar c)
{
  int ibuf = 0;

 /* If the font max is the same as the font ave width then this
  * function is a no-op.
  */
  if (!font_dualwidth)
    return 1;

 /* Speedup, I know of no font where ascii is the wrong width */
  if (c >= ' ' && c <= '~')
    return 1;

  SelectObject(dc, fonts[FONT_NORMAL]);
  if (!GetCharWidth32W(dc, c, c, &ibuf))
    return 0;

  ibuf += font_width / 2 - 1;
  ibuf /= font_width;

  return ibuf;
}

/* Try to combine a base and combining character into a precomposed one.
 * Returns 0 if unsuccessful.
 */
wchar
win_combine_chars(wchar c, wchar cc)
{
  wchar cs[2];
  int len = FoldStringW(MAP_PRECOMPOSED, (wchar[]){c, cc}, 2, cs, 2);
  return len == 1 ? *cs : 0;
}

void
win_set_colour(colour_i i, colour c)
{
  if (i >= COLOUR_NUM)
    return;
  colours[i] = c;
  switch (i) {
    when FG_COLOUR_I:
      colours[BOLD_FG_COLOUR_I] = brighten(c);
    when BG_COLOUR_I:
      colours[BOLD_BG_COLOUR_I] = brighten(c);
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

colour win_get_colour(colour_i i) { return i < COLOUR_NUM ? colours[i] : 0; }

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
