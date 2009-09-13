// wintext.c (part of mintty)
// Copyright 2008-09  Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "config.h"
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
static int fontflag[FONT_MAXNO];

enum bold_mode bold_mode;

static enum {
  UND_LINE, UND_FONT
} und_mode;
static int descent;

// Current font size (with any zooming)
int font_size; 

// Font screen dimensions
int font_width, font_height;
static bool font_dualwidth;

bool font_ambig_wide;

COLORREF colours[NALLCOLOURS];

#define CLEARTYPE_QUALITY 5

static uint
get_font_quality(void) {
  switch (cfg.font_quality) {
    when FQ_ANTIALIASED: return ANTIALIASED_QUALITY;
    when FQ_NONANTIALIASED: return NONANTIALIASED_QUALITY;
    when FQ_CLEARTYPE: return CLEARTYPE_QUALITY;
    otherwise: return DEFAULT_QUALITY;
  }
}

static HFONT
create_font(int weight, bool underline)
{
  return 
    CreateFont(
      font_height, font_width, 0, 0, weight, false, underline, false,
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
win_init_fonts(void)
{
  TEXTMETRIC tm;
  int fontsize[3];
  int i;
  int fw_dontcare, fw_bold;

  for (i = 0; i < FONT_MAXNO; i++)
    fonts[i] = null;

  bold_mode = cfg.bold_as_bright ? BOLD_COLOURS : BOLD_FONT;
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
    font_size > 0
    ? -MulDiv(font_size, GetDeviceCaps(dc, LOGPIXELSY), 72)
    : font_size;
  font_width = 0;

  fonts[FONT_NORMAL] = create_font(fw_dontcare, false);

  GetObject(fonts[FONT_NORMAL], sizeof (LOGFONT), &lfont);

  SelectObject(dc, fonts[FONT_NORMAL]);
  GetTextMetrics(dc, &tm);

  font_height = tm.tmHeight;
  font_width = tm.tmAveCharWidth;
  font_dualwidth = (tm.tmMaxCharWidth >= tm.tmAveCharWidth * 3 / 2);
  
  float latin_char_width, greek_char_width, line_char_width;
  GetCharWidthFloatW(dc, 0x0041, 0x0041, &latin_char_width);
  GetCharWidthFloatW(dc, 0x03B1, 0x03B1, &greek_char_width);
  GetCharWidthFloatW(dc, 0x2500, 0x2500, &line_char_width);
  
  font_ambig_wide =
    greek_char_width >= latin_char_width * 1.5 ||
    line_char_width  >= latin_char_width * 1.5;

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

void
win_deinit_fonts(void)
{
  int i;
  for (i = 0; i < FONT_MAXNO; i++) {
    if (fonts[i])
      DeleteObject(fonts[i]);
    fonts[i] = 0;
    fontflag[i] = 0;
  }
}

static HDC dc;

static bool update_pending;

void
win_paint(void)
{
  HideCaret(wnd);

  PAINTSTRUCT p;
  dc = BeginPaint(wnd, &p);

  term_invalidate(
    (p.rcPaint.left - PADDING) / font_width,
    (p.rcPaint.top - PADDING) / font_height,
    (p.rcPaint.right - PADDING - 1) / font_width,
    (p.rcPaint.bottom - PADDING - 1) / font_height
  );

  if (!update_pending)
    term_paint();

  if (p.fErase || p.rcPaint.left < PADDING ||
      p.rcPaint.top < PADDING ||
      p.rcPaint.right >= PADDING + font_width * term.cols ||
      p.rcPaint.bottom >= PADDING + font_height * term.rows) {
    HBRUSH fillcolour, oldbrush;
    HPEN edge, oldpen;
    colour bg_colour = colours[term.rvideo ? 256 : 258];
    fillcolour = CreateSolidBrush(bg_colour);
    oldbrush = SelectObject(dc, fillcolour);
    edge = CreatePen(PS_SOLID, 0, bg_colour);
    oldpen = SelectObject(dc, edge);

    IntersectClipRect(dc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
                      p.rcPaint.bottom);

    ExcludeClipRect(dc, PADDING, PADDING,
                    PADDING + font_width * term.cols,
                    PADDING + font_height * term.rows);

    Rectangle(dc, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right,
              p.rcPaint.bottom);

    SelectObject(dc, oldbrush);
    DeleteObject(fillcolour);
    SelectObject(dc, oldpen);
    DeleteObject(edge);
  }
  SelectObject(dc, GetStockObject(SYSTEM_FONT));
  SelectObject(dc, GetStockObject(WHITE_PEN));
  
  EndPaint(wnd, &p);
  
  ShowCaret(wnd);
}

void
win_update(void)
{
  if (update_pending) {
    KillTimer(wnd, (UINT_PTR)win_update);
    update_pending = false;
  }
  dc = GetDC(wnd);
  term_update();
  ReleaseDC(wnd, dc);
}

void
win_schedule_update(void)
{
  if (!update_pending) {
    SetTimer(wnd, (UINT_PTR)win_update, 10, null);
    update_pending = true;
  }
}

/*
 * This is a wrapper to ExtTextOut() to force Windows to display
 * the precise glyphs we give it. Otherwise it would do its own
 * bidi and Arabic shaping, and we would end up uncertain which
 * characters it had put where.
 */
static void
exact_textout(int x, int y, CONST RECT * lprc, ushort * lpString,
              UINT cbCount, CONST INT * lpDx, int opaque)
{
  GCP_RESULTSW gcpr;
  char *buffer = newn(char, cbCount * 2 + 2);
  char *classbuffer = newn(char, cbCount);
  memset(&gcpr, 0, sizeof (gcpr));
  memset(buffer, 0, cbCount * 2 + 2);
  memset(classbuffer, GCPCLASS_NEUTRAL, cbCount);

  gcpr.lStructSize = sizeof (gcpr);
  gcpr.lpGlyphs = (void *) buffer;
  gcpr.lpClass = (void *) classbuffer;
  gcpr.nGlyphs = cbCount;

  GetCharacterPlacementW(dc, lpString, cbCount, 0, &gcpr,
                         FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);

  ExtTextOut(dc, x, y,
             ETO_GLYPH_INDEX | ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0), lprc,
             buffer, cbCount, lpDx);
}

/*
 * The exact_textout() wrapper, unfortunately, destroys the useful
 * Windows `font linking' behaviour: automatic handling of Unicode
 * code points not supported in this font by falling back to a font
 * which does contain them. Therefore, we adopt a multi-layered
 * approach: for any potentially-bidi text, we use exact_textout(),
 * and for everything else we use a simple ExtTextOut as we did
 * before exact_textout() was introduced.
 */
static void
general_textout(int x, int y, CONST RECT * lprc, ushort * lpString,
                UINT cbCount, CONST INT * lpDx, int opaque)
{
  int i, j, xp, xn;
  RECT newrc;

  xp = xn = x;

  for (i = 0; i < (int) cbCount;) {
    int rtl = is_rtl(lpString[i]);

    xn += lpDx[i];

    for (j = i + 1; j < (int) cbCount; j++) {
      if (rtl != is_rtl(lpString[j]))
        break;
      xn += lpDx[j];
    }

   /*
    * Now [i,j) indicates a maximal substring of lpString
    * which should be displayed using the same textout
    * function.
    */
    if (rtl) {
      newrc.left = lprc->left + xp - x;
      newrc.right = lprc->left + xn - x;
      newrc.top = lprc->top;
      newrc.bottom = lprc->bottom;
      exact_textout(xp, y, &newrc, lpString + i, j - i, lpDx + i, opaque);
    }
    else {
      newrc.left = lprc->left + xp - x;
      newrc.right = lprc->left + xn - x;
      newrc.top = lprc->top;
      newrc.bottom = lprc->bottom;
      ExtTextOutW(dc, xp, y, ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0), &newrc,
                  lpString + i, j - i, lpDx + i);
    }

    i = j;
    xp = xn;
  }

  assert(xn - x == lprc->right - lprc->left);
}

static void
another_font(int fontno)
{
  int basefont;
  int fw_dontcare, fw_bold;
  int u, w, x;
  char *s;

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
  s = cfg.font.name;
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
               get_font_quality(), FIXED_PITCH | FF_DONTCARE, s);

  fontflag[fontno] = 1;
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
static void
win_text_internal(int x, int y, wchar *text, int len,
                  int chars, uint attr, int lattr)
{
  COLORREF fg, bg, t;
  int nfg, nbg, nfont;
  RECT line_box;
  int force_manual_underline = 0;
  int fnt_width, char_width;

  lattr &= LATTR_MODE;

  char_width = fnt_width = font_width * (1 + (lattr != LATTR_NORM));

  if (attr & ATTR_WIDE)
    char_width *= 2;

  int dxs[len];
  for (int i = 0; i < len; i++)
    dxs[i] = char_width;
  if (chars < len)
    dxs[chars] = 0;

 /* Only want the left half of double width lines */
  if (lattr != LATTR_NORM && x * 2 >= term.cols)
    return;

  x *= fnt_width;
  y *= font_height;
  x += PADDING;
  y += PADDING;

  if ((attr & TATTR_ACTCURS) && term_cursor_type() == CUR_BLOCK) {
    attr &= ~(ATTR_REVERSE | ATTR_BLINK | ATTR_COLOURS);
    if (bold_mode == BOLD_COLOURS)
      attr &= ~ATTR_BOLD;

   /* cursor fg and bg */
    attr |= (260 << ATTR_FGSHIFT) | (261 << ATTR_BGSHIFT);
  }

  nfont = 0;
  switch (lattr) {
    when LATTR_NORM: // do nothing
    when LATTR_WIDE: nfont |= FONT_WIDE;
    otherwise:       nfont |= FONT_WIDE + FONT_HIGH;
  }
  if (attr & ATTR_NARROW)
    nfont |= FONT_NARROW;

  if (bold_mode == BOLD_FONT && (attr & ATTR_BOLD))
    nfont |= FONT_BOLD;
  if (und_mode == UND_FONT && (attr & ATTR_UNDER))
    nfont |= FONT_UNDERLINE;
  another_font(nfont);
  if (!fonts[nfont]) {
    if (nfont & FONT_UNDERLINE)
      force_manual_underline = 1;
   /* Don't do the same for manual bold, it could be bad news. */

    nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
  }
  another_font(nfont);
  if (!fonts[nfont])
    nfont = FONT_NORMAL;

  nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
  nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
  if (term.rvideo) {
    if (nfg >= 260)
      nfg ^= 1;
    else if (nfg >= 256)
      nfg ^= 2;
    if (nbg >= 260)
      nbg ^= 1;
    else if (nbg >= 256)
      nbg ^= 2;
  }
  if (bold_mode == BOLD_COLOURS) {
    if (attr & ATTR_BOLD) {
      if (nfg < 16)
        nfg |= 8;
      else if (nfg >= 256)
        nfg |= 1;
    }
    if (attr & ATTR_BLINK) {
      if (nbg < 16)
        nbg |= 8;
      else if (nbg >= 256)
        nbg |= 1;
    }
  }
  if (attr & ATTR_REVERSE) {
    t = nfg;
    nfg = nbg;
    nbg = t;
  }
  fg = colours[nfg];
  bg = colours[nbg];
  SelectObject(dc, fonts[nfont]);
  SetTextColor(dc, fg);
  SetBkColor(dc, bg);
  if (attr & TATTR_COMBINING)
    SetBkMode(dc, TRANSPARENT);
  else
    SetBkMode(dc, OPAQUE);
  line_box.left = x;
  line_box.top = y;
  line_box.right = x + char_width * chars;
  line_box.bottom = y + font_height;

 /* Only want the left half of double width lines */
  if (line_box.right > font_width * term.cols + PADDING)
    line_box.right = font_width * term.cols + PADDING;

 /* print Glyphs as they are, without Windows' Shaping */
  general_textout(x, y - font_height * (lattr == LATTR_BOT),
                  &line_box, text, len, dxs, !(attr & TATTR_COMBINING));

 /* And the shadow bold hack. */
  if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
    SetBkMode(dc, TRANSPARENT);
    ExtTextOutW(dc, x - 1,
                y - font_height * (lattr == LATTR_BOT),
                ETO_CLIPPED, &line_box, text, len, dxs);
  }
  if (lattr != LATTR_TOP &&
      (force_manual_underline ||
       (und_mode == UND_LINE && (attr & ATTR_UNDER)))) {
    HPEN oldpen;
    int dec = descent;
    if (lattr == LATTR_BOT)
      dec = dec * 2 - font_height;

    oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, fg));
    MoveToEx(dc, x, y + dec, null);
    LineTo(dc, x + chars * char_width, y + dec);
    oldpen = SelectObject(dc, oldpen);
    DeleteObject(oldpen);
  }
}

/*
 * Wrapper that handles combining characters.
 */
void
win_text(int x, int y, wchar *text, int len, uint attr, int lattr)
{
  if (attr & TATTR_COMBINING) {
    attr &= ~TATTR_COMBINING;
    do {
      uint n = 1 + ((*text & 0xFC00) == 0xD800);
      win_text_internal(x, y, text, n, 1, attr, lattr);
      len -= n, text += n;
      attr |= TATTR_COMBINING;
    } while (len > 0);
  }
  else
    win_text_internal(x, y, text, len, len, attr, lattr);
}

void
win_cursor(int x, int y, wchar *text, int len, uint attr, int lattr)
{
  int fnt_width;
  int char_width;
  int cursor_type = term_cursor_type();
  colour cursor_colour = colours[261 - term.rvideo];

  lattr &= LATTR_MODE;

  if ((attr & TATTR_ACTCURS) && cursor_type == CUR_BLOCK) {
    if (*text != UCSWIDE) {
      win_text(x, y, text, len, attr, lattr);
      return;
    }
    cursor_type = CUR_LINE;
    attr |= TATTR_RIGHTCURS;
  }

  fnt_width = char_width = font_width * (1 + (lattr != LATTR_NORM));
  if (attr & ATTR_WIDE)
    char_width *= 2;
  x = x * fnt_width + PADDING;
  y = y * font_height + PADDING;

  HPEN oldpen = SelectObject(dc, CreatePen(PS_SOLID, 0, cursor_colour));
  HBRUSH oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  switch(cursor_type) {
    when CUR_BLOCK:
      if (attr & TATTR_PASCURS)
        Rectangle(dc, x, y, x + char_width, y + font_height);
    when CUR_LINE:
      if (attr & TATTR_RIGHTCURS)
        x += char_width - 1;
      if (attr & TATTR_ACTCURS)
        Rectangle(dc, x, y, x + 1, y + font_height);
      else if (attr & TATTR_PASCURS) {
        for (int dy = 0; dy < font_height; dy += 2)
          SetPixel(dc, x, y + dy, cursor_colour);
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
  SelectObject(dc, oldbrush);
  DeleteObject(SelectObject(dc, oldpen));
}

/* This function gets the actual width of a character in the normal font.
 */
int
win_char_width(int uc)
{
  int ibuf = 0;

 /* If the font max is the same as the font ave width then this
  * function is a no-op.
  */
  if (!font_dualwidth)
    return 1;

 /* Speedup, I know of no font where ascii is the wrong width */
  if (uc >= ' ' && uc <= '~')
    return 1;

  SelectObject(dc, fonts[FONT_NORMAL]);
  if (!GetCharWidth32W(dc, uc, uc, &ibuf))
    return 0;

  ibuf += font_width / 2 - 1;
  ibuf /= font_width;

  return ibuf;
}

void
win_set_sbar(int total, int start, int page)
{
  if (cfg.scrollbar) {
    SCROLLINFO si;
    si.cbSize = sizeof (si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;	
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (wnd)
      SetScrollInfo(wnd, SB_VERT, &si, true);
  }
}

static colour 
brighter(colour c)
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

void
win_set_colour(uint n, colour c)
{
  if (n >= 262)
    return;
  colours[n] = c;
  switch (n) {
    when 256:
      colours[257] = brighter(c);
    when 258:
      colours[259] = brighter(c);
    when 261: {
      // Set the colour of text under the cursor to whichever of foreground
      // and background colour is further away.
      colour fg = colours[256], bg = colours[258];
      colours[260] = colour_dist(c, fg) > colour_dist(c, bg) ? fg : bg;
    }
  }
  // Redraw everything.
  win_invalidate_all();
}

colour win_get_colour(uint n) { return n < 262 ? colours[n] : 0; }

void
win_reconfig_palette(void)
{
  win_set_colour(FG_COLOUR_I, cfg.fg_colour);
  win_set_colour(BG_COLOUR_I, cfg.bg_colour);
  win_set_colour(CURSOR_COLOUR_I, cfg.cursor_colour);
}

void
win_reset_colours(void)
{
  static const colour
  ansi_colours[16] = {
    0x000000, 0x0000BF, 0x00BF00, 0x00BFBF,
    0xBF0000, 0xBF00BF, 0xBFBF00, 0xBFBFBF,
    0x404040, 0x4040FF, 0x40FF40, 0x40FFFF,
    0xFF4040, 0xFF40FF, 0xFFFF40, 0xFFFFFF
  };
  memcpy(colours, ansi_colours, sizeof ansi_colours);

  // Colour cube
  int i = 16;
  for (uint r = 0; r < 6; r++)
    for (uint g = 0; g < 6; g++)
      for (uint b = 0; b < 6; b++)
        colours[i++] = RGB (r ? r * 40 + 55 : 0,
                            g ? g * 40 + 55 : 0,
                            b ? b * 40 + 55 : 0);
  
  // Grayscale
  for (uint s = 0; s < 24; s++) {
    uint c = s * 10 + 8;
    colours[i++] = RGB(c,c,c);
  }

  // Foreground, background, cursor
  win_reconfig_palette();
}

void
win_invalidate_all(void)
{
  InvalidateRect(wnd, null, true);
}
