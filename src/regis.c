// ReGIS (part of mintty)
// Copyright 2025-2026 Thomas Wolff
// Licensed under the terms of the GNU General Public License v3 or later.

// Implement ReGIS graphics instructions
// https://vt100.net/docs/vt3xx-gp/ chapters 1..12
// EK-VT125-GI-001_VT125_ReGIS_Primer_May82.pdf
// (http://bitsavers.org/pdf/dec/terminal/vt125/)

#include "charset.h"  // cs__mbstowcs
#include "config.h"  // red, green, blue, regis_fg_colour
#include "regis.h"
#include <math.h>
#include <windows.h>


#if CYGWIN_VERSION_API_MINOR >= 74
#define use_gdiplus
#endif


#ifdef debug_regis

#include <signal.h>
extern void cygwin_stackdump(void);

static void
sigsegv(int sig)
{
  signal(sig, SIG_DFL);
  printf("catch %d\n", sig);
  fflush(stdout);
  cygwin_stackdump();
}

#endif

#ifdef use_gdiplus

#include <gdiplus/gdiplus.h>
#include <gdiplus/gdiplusflat.h>
#include <gdiplus/gdipluscolor.h>

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
    printf("[41m%d %s[m [%s]\n", s, s >= 0 && s < lengthof(gps) ? gps[s] : "?", tag);
}

#define gp(gpcmd)	gpcheck(#gpcmd, gpcmd)

#else

#define gpcheck(tag, s)	(void)s
#define gp(gpcmd)	gpcmd

#endif


#define GpARGB(r, g, b)	(255 << 24) | (r << 16) | (g << 8) | b
static GpPen * gpen = 0;


static void
gdiplus_init(void)
{
  static GdiplusStartupInput gi = {1, NULL, FALSE, FALSE};
  static ULONG_PTR gis = 0;
  if (!gis) {
    gp(GdiplusStartup(&gis, &gi, NULL));
  }
}

#else  // #ifdef use_gdiplus
enum HatchStyle {HatchStyleDiagonalCross};
typedef struct PointF {
  float X;
  float Y;
} GpPointF;
#endif


#if CYGWIN_VERSION_API_MINOR < 75
#define roundf(f) ((int)(f + 0.5 - (f < 0)))
#endif


static inline void
println(char * s)
{
  char * ln = strchr(s, '\r');
  if (!ln)
    ln = strchr(s, '\n');
  if (ln)
    printf("%.*s\n", (int)(ln - s), s);
  else
    printf("%s\n", s);
}


struct text_controls {
  uint size;
  int tilt;
  bool italic;
} text_controls;

struct write_controls {
  int PV;
  int pattern;
  int patternM;
  short negative;
  COLORREF foreground;
  COLORREF background;
  int plane;
  char writing_style;
  short shading;
  bool shade_vert;
  int shade_x, shade_y;
  wchar hatch;
  enum HatchStyle ghatch;
  // Text Options
  struct text_controls text;
} write_controls, store_write_controls;

static bool interpolating;
static bool centerspec;
static int angle;

// by turning a number of variables into floats, we can smooth out 
// edges which appear as transition artefacts when combining lines 
// and arcs to a (filled) path
//typedef int flint;
typedef float flint;

// current cursor position
static flint curr_rx = 0;
static flint curr_ry = 0;

// scanned coordinates, based on cursor position
static flint new_rx = 0;
static flint new_ry = 0;
// scanned x/y values
static int scan_rx = 0;
static int scan_ry = 0;


static int
hatch(char t)
{
  if (strchr("#+EFHLT", t))
    return HS_CROSS;  // Horizontal and vertical crosshatch
  else if (strchr("VXvx", t))
    return HS_DIAGCROSS;  // 45-degree crosshatch
  else if (strchr("/", t))
    return HS_BDIAGONAL;  // 45-degree upward left-to-right hatch
  else if (strchr("\\`", t))
    return HS_FDIAGONAL;  // 45-degree downward left-to-right hatch
  else if (strchr("=-_", t))
    return HS_HORIZONTAL;  // Horizontal hatch
  else if (strchr("!\"'|", t))
    return HS_VERTICAL;  // Vertical hatch
  else
    return HS_DIAGCROSS;
}

static enum HatchStyle
ghatch(char t)
{
#ifdef use_gdiplus
  switch (t) {
    when '#': return HatchStyleCross;
    when 'X': return HatchStyleDiagonalCross;
    when '\\': return HatchStyleForwardDiagonal;
    when '/': return HatchStyleBackwardDiagonal;
    when '=': return HatchStyleHorizontal;
    when '|': return HatchStyleVertical;

    when '+': return HatchStyleLargeGrid;
    when '_': return HatchStyle05Percent;
    when '1': return HatchStyle10Percent;
    when '2': return HatchStyle20Percent;
    when '{': return HatchStyle25Percent;
    when '3': return HatchStyle30Percent;
    when '4': return HatchStyle40Percent;
    when '5': return HatchStyle50Percent;
    when '6': return HatchStyle60Percent;
    when '7': return HatchStyle70Percent;
    when '}': return HatchStyle75Percent;
    when '8': return HatchStyle80Percent;
    when '9': return HatchStyle90Percent;

// !"%&'()*,-.0:;<>?@EFGJKLMNOQRUVYZ[]^_
//`aefijklmnopqrstuvwxy{}
    when 'a': return HatchStyleLightDownwardDiagonal;
    when 'e': return HatchStyleLightUpwardDiagonal;
    when 'f': return HatchStyleDarkDownwardDiagonal;
    when 'i': return HatchStyleDarkUpwardDiagonal;
    when 'j': return HatchStyleWideDownwardDiagonal;
    when 'k': return HatchStyleWideUpwardDiagonal;
    when 'l': return HatchStyleLightVertical;
    when 'm': return HatchStyleLightHorizontal;
    when 'n': return HatchStyleNarrowVertical;
    when 'o': return HatchStyleNarrowHorizontal;
    when 'p': return HatchStyleDarkVertical;
    when 'q': return HatchStyleDarkHorizontal;
    when 'r': return HatchStyleDashedDownwardDiagonal;
    when 's': return HatchStyleDashedUpwardDiagonal;
    when 't': return HatchStyleDashedHorizontal;
    when 'u': return HatchStyleDashedVertical;

    when 'c': return HatchStyleSmallConfetti;
    when 'C': return HatchStyleLargeConfetti;
    when 'z': return HatchStyleZigZag;
    when '~': return HatchStyleWave;
    when 'b': return HatchStyleDiagonalBrick;
    when 'B': return HatchStyleHorizontalBrick;
    when 'W': return HatchStyleWeave;
    when 'P': return HatchStylePlaid;
    when '.': return HatchStyleDivot;
    when 'd': return HatchStyleDottedGrid;
    when 'D': return HatchStyleDottedDiamond;
    when 'U': return HatchStyleShingle;
    when 'T': return HatchStyleTrellis;
    when 'S': return HatchStyleSphere;
    when 'g': return HatchStyleSmallGrid;
    when 'h': return HatchStyleSmallCheckerBoard;
    when 'H': return HatchStyleLargeCheckerBoard;
    when 'A': return HatchStyleOutlinedDiamond;
    when '$': return HatchStyleSolidDiamond;
  }
#else
  (void)t;
#endif
  return 0;
}

static inline void
printbin(unsigned long bin, unsigned int digits)
{
  for (unsigned long m = (unsigned long)1 << (digits - 1); m; m >>= 1) {
    printf ("%c", (bin & m) ? '1' : '0');
  }
}

static DWORD dashpat[8];
static float gdashpat[8];
static int dashi;

static unsigned short
binary_pattern(int pattern, int patternM, short negative, float scale)
{
static short _binpat[] =
               {0x00, 0xFF, 0xF0, 0xE4, 0xAA, 0xEA, 0x88, 0x84, 0xC8, 0x86};
  unsigned short binpat;
  if (pattern >= 0 && pattern <= 9)
    binpat = _binpat[pattern];
  else {
    // convert decimal pattern spec (8 lowest digits) to binary pattern
    unsigned int pat = pattern;
    binpat = 0;
    for (int i = 7; i >= 0; i--) {
      binpat >>= 1;
      if (pat % 10)
        binpat |= 0x80;
      pat /= 10;
    }
  }
  if (negative)
    binpat ^= 0xFF;
  binpat &= 0xFF;
  //printf("%d->", pattern); printbin(binpat, 8);

  // set dash pattern array (for both GDI and GDI+)
  unsigned short pata = binpat;
  int bits = 0;
  int patbits = 0;
  dashi = 0;
  bool scanbit = 1;
  while (bits <= 8) {
    if (bits < 8 && !!(pata & 0x80) == scanbit)
      patbits ++;
    else {
      dashpat[dashi] = patbits * patternM / 2;
      gdashpat[dashi] = patbits * patternM * 1.25 * scale;
      dashi ++;
      patbits = 1;
      scanbit = !scanbit;
    }
    pata <<= 1;
    bits ++;
  }
#ifdef debug_pattern
  printf("dash pattern "); printbin(binpat, 8); printf(" ->");
  for (int i = 0; i < dashi; i++)
    printf(" %f", gdashpat[i]);
  printf("\n");
#endif

  return binpat;
}

#ifndef use_gdiplus

static int
pattern_density(int pattern, short negative)
{
  unsigned short binpat = binary_pattern(pattern, 1, negative, 1);

  // count number of 1s, detect dashes (sequence of 111) and dots (010)
  short ones = 0, dashes = 0, dots = 0;
  // duplicate pattern to simulate circular detection of dashes and dots
  binpat = (binpat << 8) | binpat;
  for (int i = 7; i >= 0; i--) {
    if (binpat & 1)
      ones ++;
    if ((binpat & 7) == 7)
      dashes ++;
    if ((binpat & 7) == 2)
      dots ++;
    binpat >>= 1;
  }
  //printf(": ones %d dashes %d dots %d\n", ones, dashes, dots);

  // approximate by system line pattern
  if (ones >= 7)
    return PS_SOLID;
  else if (!ones)
    return PS_NULL;
  else if (!dashes)
    return PS_DOT;
  else if (!dots)
    return PS_DASH;
  else if (dots < 2)
    return PS_DASHDOT;
  else
    return PS_DASHDOTDOT;
}

static void
set_pen(HDC dc, struct write_controls * controls)
{
static HPEN pen = 0;

  COLORREF c = controls->foreground;

  if (pen)
    DeleteObject(pen);
  int style = pattern_density(controls->pattern, controls->negative);
  //printf("set_pen %06X: %d\n", (int)c, style);

  if (style == PS_SOLID || style == PS_NULL)
    pen = CreatePen(style, 1, c);
  else {
    LOGBRUSH lb = {BS_SOLID, c, 0};
    pen = ExtCreatePen(PS_USERSTYLE, 1, &lb, dashi, dashpat);
  }

  SelectObject(dc, pen);
  // (?) prevent colour mangling of dashed or dotted lines
  SetBkMode(dc, TRANSPARENT);
}

#endif

#ifdef use_gdiplus

static enum DashStyle
gpattern_density(struct write_controls * controls, float scale)
{
  int pattern = controls->pattern;
  int patternM = controls->patternM;
  short negative = controls->negative;
  unsigned short binpat = binary_pattern(pattern, patternM, negative, scale);

  // count number of 1s, detect dashes (sequence of 111) and dots (010)
  short ones = 0, dashes = 0, dots = 0;
  // duplicate pattern to simulate circular detection of dashes and dots
  binpat = (binpat << 8) | binpat;
  for (int i = 7; i >= 0; i--) {
    if (binpat & 1)
      ones ++;
    if ((binpat & 7) == 7)
      dashes ++;
    if ((binpat & 7) == 2)
      dots ++;
    binpat >>= 1;
  }
  //printf(": ones %d dashes %d dots %d\n", ones, dashes, dots);

  // approximate by system line pattern
  if (ones >= 7)
    return DashStyleSolid;
  else if (!ones)
    return DashStyleCustom;  // no null style in GDI+
  else if (!dashes)
    return DashStyleDot;
  else if (!dots)
    return DashStyleDash;
  else if (dots < 2)
    return DashStyleDashDot;
  else
    return DashStyleDashDotDot;
}

static void
set_gpen(struct write_controls * controls, float scale)
{
  if (gpen)
    gp(GdipDeletePen(gpen));

  // determine dash style
  enum DashStyle gstyle = gpattern_density(controls, scale);

  // create Pen
  COLORREF c = controls->foreground;
  ARGB fg = GpARGB(red(c), green(c), blue(c));
  if (gstyle == DashStyleCustom)
    fg = 0;  // fully transparent for null dash style
  gp(GdipCreatePen1(fg, scale, UnitPixel, &gpen));

  // SetDashStyle
  gp(GdipSetPenDashStyle(gpen, gstyle));
  // SetDashPattern for dashed/dotted/custom styles
  if (gstyle != DashStyleSolid)
    gp(GdipSetPenDashArray(gpen, gdashpat, dashi));
}

#endif


static COLORREF
mapcol(int coli)
{
#define RGBval(r, g, b)	RGB(r * 255 / 100, g * 255 / 100, b * 255 / 100);
  switch (coli) {  // Table 2-3 VT340 Default Color Map
    when 0 : return RGBval(0, 0, 0);
    when 1 : return RGBval(20, 20, 80);
    when 2 : return RGBval(80, 13, 13);
    when 3 : return RGBval(20, 80, 20);
    when 4 : return RGBval(80, 20, 80);
    when 5 : return RGBval(20, 80, 80);
    when 6 : return RGBval(80, 80, 20);
    when 7 : return RGBval(53, 53, 53);
    when 8 : return RGBval(26, 26, 26);
    when 9 : return RGBval(33, 33, 60);
    when 10: return RGBval(60, 26, 26);
    when 11: return RGBval(33, 60, 33);
    when 12: return RGBval(60, 33, 60);
    when 13: return RGBval(33, 60, 60);
    when 14: return RGBval(60, 60, 33);
    when 15: return RGBval(80, 80, 80);
  }
  return RGBval(50, 50, 50);
}

#define wcsisprefix(p, s)	(wcsncmp (s, p, wcslen (p)) == 0)

/*
   Some fonts handle wide and combining characters well out-of-the-box, 
   while others produce glitches and accent misplacement.
   A workaround below handles the "bad fonts" but has some small 
   drawback in base character placement.
   So the workaround is switched conditionally based on a list of 
   known "good fonts".
 */
static bool
isgoodfont(wstring fn)
{
  wstring good_fonts[] = {
    W("Cascadia"),
    W("Code New Roman"),
    W("Consola Mono"),
    W("Consolas"),
    W("Cousine"),
    W("DejaVu"),
    W("Droid"),
    W("Everson"),
    W("Fantasque"),
    W("Fira"),
    W("FreeMono"),
    W("Hasklig"),
    W("Iosevka"),
    W("Iosevka"),
    W("JetBrains Mono"),
    W("Liberation Mono"),
    W("Meslo"),
    W("MS Gothic"),
    W("Noto Mono"),
    W("Office Code Pro"),
    W("SauceCodePro"),
    W("Terminus"),
  };
  for (uint i = 0; i < lengthof(good_fonts); i++)
    if (wcsisprefix(good_fonts[i], fn))
      return true;
  return false;
}

void
regis_text(HDC dc, float scale, struct write_controls * controls, wchar * s)
{
  struct {
    int w, h;
  } sizes[17] = {
    {9, 10},
    {9, 20},
    {18, 30},
    {27, 45},
    {36, 60},
    {45, 75},
    {54, 90},
    {63, 105},
    {72, 120},
    {81, 135},
    {90, 150},
    {99, 165},
    {108, 180},
    {117, 195},
    {126, 210},
    {135, 225},
    {144, 240},
  };
  int w = sizes[controls->text.size].w;
  int h = sizes[controls->text.size].h;
  int tilt = controls->text.tilt * 10;  // ReGIS: 0 is right, 90 is upwards
  int s_rx = curr_rx;
  int s_ry = curr_ry;

  void move(int x, int y, bool move_anchor) {
    int dx, dy;
    if (tilt) {
      float angle = ((float)controls->text.tilt) * M_PI / 180.0;
      dx = x * cosf(angle);
      dy = - x * sinf(angle);  // vertical axis goes downward
      if (y) {
        dx += y * cosf(angle - M_PI / 2.0);
        dy += - y * sinf(angle - M_PI / 2.0);  // vertical axis goes downward
      }
    }
    else {
      dx = x;
      dy = y;
    }
    curr_rx += dx;
    curr_ry += dy;
    if (move_anchor) {
      s_rx += dx;
      s_ry += dy;
    }
  }

  if (!dc) {  // PV Spacing
    int pv = (long int)s;
    static bool halfhori = 0;  // balance odd half widths
    static bool halfvert = 0;  // balance odd half heights
    switch (pv) {
      when 0:  // move forward half width
               move((w + halfhori) / 2, 0, false);
               halfhori = !halfhori;
      when 4:  // move backward half width (44 overstrike)
               move(-(w + !halfhori) / 2, 0, false);
               halfhori = !halfhori;
      when 6:  // move down (subscript)
               move(0, (h + halfvert) / 2, false);
               halfvert = !halfvert;
      when 7:  // move down (subscript) and half spacing
               move((w + halfhori) / 2, (h + halfvert) / 2, false);
               halfvert = !halfvert;
               halfhori = !halfhori;
      when 2:  // move up (superscript)
               move(0, -(h - !halfvert) / 2, false);
               halfvert = !halfvert;
      when 1:  // move up (superscript) and half spacing
               move((w + halfhori) / 2, -(h + !halfvert) / 2, false);
               halfvert = !halfvert;
               halfhori = !halfhori;
      when 3:  // move up and half width back
               move(-(w + !halfhori) / 2, -(h + !halfvert) / 2, false);
               halfvert = !halfvert;
               halfhori = !halfhori;
      when 5:  // move down and half width back
               move(-(w + !halfhori) / 2, (h + halfvert) / 2, false);
               halfvert = !halfvert;
               halfhori = !halfhori;
    }
    return;
  }

  uint get_font_quality(void)
  {
    return
      (uchar[]){
        [FS_DEFAULT] = DEFAULT_QUALITY,
        [FS_NONE] = NONANTIALIASED_QUALITY,
        [FS_PARTIAL] = ANTIALIASED_QUALITY,
        [FS_FULL] = CLEARTYPE_QUALITY
      }[(int)cfg.font_smoothing];
  }

  wstring fn = *cfg.regis_font ? cfg.regis_font : cfg.font.name;
  HFONT f = CreateFontW(
                  h * scale, w * scale,
                  tilt,  // string angle
                  tilt,  // only effective in zoom_transform mode
                  FW_NORMAL, controls->text.italic, 0, 0,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  get_font_quality(), FIXED_PITCH | FF_DONTCARE,
                  fn);
  SelectObject(dc, f);
#ifdef debug_regis_font
  TEXTMETRIC tm;
  int tmok = GetTextMetrics(dc, &tm);
  printf("TextMetric %d h %d a %d d %d e %d i %d w %d cs %d <%ls>\n", 
         tmok, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmExternalLeading, 
         tm.tmInternalLeading, tm.tmAveCharWidth, tm.tmCharSet, fn);
#endif
  bool good_font = isgoodfont(fn);

  SetBkMode(dc, TRANSPARENT);
  if (controls->writing_style == 'E' || controls->writing_style == 'C')
    SetTextColor(dc, controls->background);
  else
    SetTextColor(dc, controls->foreground);

  int text_chunk(wchar * s, int len) {
    int dxs[len];  // character advancement in device scale
    int rdx = 0;  // string width in ReGIS scale
    for (int i = 0; i < len; i++) {
      int dx;
      int width = xcwidth(s[i]);
      if (width == 2 && !is_ambig(s[i]))
        dx = 2 * w;
      else if (width < 1)
        dx = 0;
      else
        dx = w;
      dxs[i] = dx * scale;
      rdx += dx;

      // for "bad fonts", break chunk on combining characters
      if (!good_font && !dx) {
        if (i) {
          len = i;
          dxs[i - 1] = 0;
          break;
        }
        else {
          // adjust combining character
          move(- w, 0, false);
          rdx = w;
          len = 1;
          break;
        }
      }
    }
    // anchor top left of first character at current ReGIS position
    int x = curr_rx * scale, y = curr_ry * scale;
    int opt = 0;
#ifdef text_replace_writing
    if (controls->writing_style == 'R')
      opt = ETO_OPAQUE;  // no effect
#endif
    ExtTextOutW(dc, x, y, opt, 0, s, len, dxs);

    // advance ReGIS cursor behind string in its writing direction
    move(rdx, 0, false);

    return len;
  }

  // split text into text_chunk for output,
  // interpret controls, also break on combining characters
  while (*s) {
    //printf ("text chunk %ld %d/%d", wcslen(s), curr_rx, curr_ry);
    switch (*s) {
      when '\r':
        // return to anchor
        curr_rx = s_rx; curr_ry = s_ry;
        s ++;
      when '\n':
        move(0, h, true);  // also advance anchor
        s ++;
      when '\b':
        move(-w, 0, false);
        s ++;
      when '\t':
        move(w, 0, false);
        s ++;
      otherwise: {
#if CYGWIN_VERSION_API_MINOR >= 74
        wchar * brk = wcspbrk(s, W("\r\n\b\t"));
        if (brk) {
          int len = brk - s;
          s += text_chunk (s, len);
        }
        else
#endif
        {
          int len = wcslen(s);
          s += text_chunk (s, len);
        }
      }
    }
    //printf (" -> %d/%d\n", curr_rx, curr_ry);
  }

  DeleteObject(f);
}


void
regis_init(void)
{
  // initialise ReGIS parameters
  write_controls.PV = 1;
  write_controls.pattern = 1;
  write_controls.patternM = 2;
  write_controls.negative = 0;
  //write_controls.foreground = cfg.regis_fg_colour;
  write_controls.foreground = RGB(200, 200, 200);
  //write_controls.foreground = cfg.regis_bg_colour;
  write_controls.background = RGB(0, 0, 0);
  write_controls.plane = 0xF;  // all planes
  write_controls.writing_style = 'V';
  write_controls.shading = 0;
  write_controls.shade_vert = false;
  write_controls.hatch = HS_DIAGCROSS;
  write_controls.ghatch = 0;  // HatchStyleDiagonalCross / HatchStyle50Percent
  // Text Controls
  write_controls.text.size = 1;
  write_controls.text.tilt = 0;
  write_controls.text.italic = false;

  // C state
  //interpolating = false;  // cleared on C below
  centerspec = false;

  // persistent write controls
  store_write_controls = write_controls;
}


static float h, l, s;

static void
rgbToHsl(float r, float g, float b)
{ // adapted from https://www.w3schools.com/lib/w3color.js
  r /= 255;
  g /= 255;
  b /= 255;
  float min = r;
  float max = r;
  int maxcolor = 0;
  if (g <= min)
    min = g;
  if (g >= max) {
    max = g; maxcolor = 1;
  }
  if (b <= min)
    min = b;
  if (b >= max) {
    max = b; maxcolor = 2;
  }

  //float h;
  if (maxcolor == 0)
    h = (g - b) / (max - min);
  if (maxcolor == 1)
    h = 2 + (b - r) / (max - min);
  if (maxcolor == 2)
    h = 4 + (r - g) / (max - min);

  if (isnanf(h))
    h = 0;
  h *= 60;
  if (h < 0)
    h = h + 360;
  //float l;
  l = (min + max) / 2;
  //float s;
  if (min == max) {
    s = 0;
  }
  else {
    if (l < 0.5)
      s = (max - min) / (max + min);
    else
      s = (max - min) / (2 - max - min);
  }
  //printf("h %f l %f s %f\n", h, l, s);
}

static COLORREF
hslToRgb(float hue, float sat, float light)
{ // adapted from https://www.w3schools.com/lib/w3color.js
  float t2;
  hue = hue / 60;
  if (light <= 0.5) {
    t2 = light * (sat + 1);
  } else {
    t2 = light + sat - (light * sat);
  }
  float t1 = light * 2 - t2;

  float hueToRgb(float t1, float t2, float hue) {
    if (hue < 0) hue += 6;
    else if (hue >= 6) hue -= 6;
    if (hue < 1)
      return (t2 - t1) * hue + t1;
    else if (hue < 3)
      return t2;
    else if (hue < 4)
      return (t2 - t1) * (4 - hue) + t1;
    else return t1;
  }

  float r = hueToRgb(t1, t2, hue + 2) * 255;
  float g = hueToRgb(t1, t2, hue) * 255;
  float b = hueToRgb(t1, t2, hue - 2) * 255;
  //printf("r %f g %f b %f\n", r, g, b);
  return RGB(roundf(r), roundf(g), roundf(b));
}

static void
sethls(COLORREF colr)
{
  rgbToHsl(red(colr), green(colr), blue(colr));
}

static COLORREF
fromhls(void)
{
  COLORREF colr = hslToRgb(h, s, l);
  return colr;
  // alternative: https://www.baeldung.com/cs/convert-color-hsl-rgb
}

static void
sethue(COLORREF * colr, int val)
{
  sethls(*colr);
  h = val;
  *colr = fromhls();
}

static void
setlightness(COLORREF * colr, int val)
{
  sethls(*colr);
  l = val;
  *colr = fromhls();
}

static void
setsaturation(COLORREF * colr, int val)
{
  sethls(*colr);
  s = val;
  *colr = fromhls();
}

static void
setred(COLORREF * colr, int val)
{
  *colr = RGB(val, green(*colr), blue(*colr));
}

static void
setgreen(COLORREF * colr, int val)
{
  *colr = RGB(red(*colr), val, blue(*colr));
}

static void
setblue(COLORREF * colr, int val)
{
  *colr = RGB(red(*colr), green(*colr), val);
}


#define subcmd(cmd, sub)	((cmd << 8) | sub)

/*
   Draw a ReGIS program.
   A mixed parsing strategy of the ReGIS string evolved during development:
   - recursive parsing (function regis_chunk) especially for parsing 
     sub-commands as attached in parentheses
   - passive parsing of command parameters (like coordinate pairs [..., ...]) 
     where the actual command gets considered as noted in a parsing state
   - active parsing of command parameters in some cases
   Particularly the passive, state-related parsing approach was induced 
   by the really weird ReGIS format which lacks syntactic structure that 
   would reflect logical structure.
   ReGIS graphics rendering is invoked on-the-fly as being parsed, 
   so it's an interpreter, no storage of parsing structure is involved. 
   The only additional storage is the list of defined "macrographs".
 */
void
regis_draw(HDC dc, float scale, int rwidth, int rheight, int rmode, uchar * regis, flush_fn flush)
{

static bool regis_init_done = false;

  float tension = 0.6;
  if (*cfg.regis_tension)
    sscanf(cfg.regis_tension, "%f", &tension);

  if ((rmode & 1) || !regis_init_done) {
    // reset ReGIS drawing parameters
    regis_init();

    // home ReGIS cursor
    //regis_home();
    curr_rx = 0;
    curr_ry = 0;

    regis_init_done = true;
  }

#ifdef use_gdiplus
  gdiplus_init();

  GpGraphics * gr;
  gp(GdipCreateFromHDC(dc, &gr));
  gp(GdipSetSmoothingMode(gr, SmoothingModeAntiAlias8x8));
#else
  (void)rwidth; (void)rheight;
#endif

#ifdef debug_regis
  printf("[43;30mregis_draw scale %f[K[m\n", scale);
  signal(SIGSEGV, sigsegv);

  if (!cfg.regis_grid)
    cfg.regis_grid = 100;
#endif

  void screen_grid(int d) {
    float w = rwidth / scale;
    float h = rheight / scale;
#ifdef grid_use_gdiplus
    ARGB fg = GpARGB(33, 33, 33);
    GpPen * gridpen;
    GdipCreatePen1(fg, 1.0, UnitPixel, &gridpen);

    for (int i = d; i < w; i += d)
      GdipDrawLine(gr, gridpen, i * scale, 0, i * scale, (h - 1) * scale);
    for (int i = d; i < h; i += d)
      GdipDrawLine(gr, gridpen, 0, i * scale, (w - 1) * scale, i * scale);
    GdipDeletePen(gridpen);
#else
    COLORREF c = RGB(33, 33, 33);
    HPEN pen = CreatePen(PS_SOLID , 1, c);
    SelectObject(dc, pen);

    for (int i = d; i < w; i += d) {
      MoveToEx(dc, i * scale, 0, 0);
      LineTo(dc, i * scale, rheight);
    }
    for (int i = d; i < h; i += d) {
      MoveToEx(dc, 0, i * scale, 0);
      LineTo(dc, rwidth, i * scale);
    }
    DeleteObject(pen);
#endif
  }

  if (cfg.regis_grid)
    screen_grid(cfg.regis_grid);


// Position stack for (S) (B) (E) commands.
#define stacklen 16
static struct {
  char cmd;
  char f;
  int rx;
  int ry;
  } posstack[stacklen];
static int posi = 0;


// Macrographs
static struct macro {
  char * macro;
  bool invoked;
} macro[26] = {
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
  {0, false},
};

  void clear_macro(int mi) {
    if (macro[mi].macro) {
      free(macro[mi].macro);
      macro[mi].macro = 0;
      macro[mi].invoked = false;
    }
  }

  if (rmode & 1) {
    for (uint mi = 0; mi < lengthof(macro); mi++)
      clear_macro(mi);
  }

  struct macro_stack {
    int mi;
    char * ret;
  } macro_stack[26];
  int macro_stacki = 0;


  char * regis_string(char * r, wchar * * text) {
    if (text)
      *text = 0;
    if (*r == '"' || *r == '\'') {
      char q = *r++;
      char * str = strdup(r);
      char * s = str;
      while (*r) {
        if (*r == q) {
          // end of string chunk
          r++;
          if (*r == q) {
            // double-quoted quote ("" or '')
            *s++ = q;
            r++;
          }
          else {
            // maybe string chunk concatenation "...", "..."
            while (*r && (*r <= ' ' || *r == ',')) {
              r++;  // skip string chunk separators
            }
            if (*r == '"' || *r == '\'') {
              q = *r++;
              // continue string
            }
            else
              break;
          }
        }
        else {
          *s++ = *r++;
        }
      }
      *s = 0;  // end of string
      if (text)
        *text = cs__mbstowcs(str);
      free(str);
    }
    return r;
  }


#ifdef use_gdiplus

  GpPath * gpath = 0;

  void init_gpath(void)
  {
    if (!gpath) {
      // choose FillModeAlternate or FillModeWinding
      GpFillMode gmode = FillModeAlternate;

      gp(GdipCreatePath(gmode, &gpath));
    }
  }

  void fill_gpath(struct write_controls * controls, bool fillcmd)
  {
    if (gpath) {
      if (!fillcmd && controls->shading) {
        // close the path on the reference line
        float currx = curr_rx * scale;
        float curry = curr_ry * scale;
        float refx = controls->shade_x * scale;
        float refy = controls->shade_y * scale;
        if (controls->shade_vert)
          gp(GdipAddPathLine(gpath, refx, curry, refx, refy));
        else
          gp(GdipAddPathLine(gpath, currx, refy, refx, refy));
        //gp(GdipClosePathFigure(gpath));  // makes no difference
      }

      // clear background for Erase or Replace mode
      if (controls->writing_style == 'E' || controls->writing_style == 'R') {
        COLORREF c = controls->negative ? controls->foreground : controls->background;
        ARGB bg = GpARGB(red(c), green(c), blue(c));
        //printf("     {bg %06X}\n", bg);
        GpSolidFill * gbr;
        gp(GdipCreateSolidFill(bg, &gbr));
        gp(GdipFillPath(gr, gbr, gpath));
        gp(GdipDeleteBrush(gbr));
      }
      // fill foreground unless for Erase mode
      if (controls->writing_style != 'E') {
        COLORREF c = controls->foreground;
        ARGB fg = GpARGB(red(c), green(c), blue(c));
        //printf("     {fg %06X}\n", fg);
        GpBrush * gbr;
        if (controls->ghatch) {
          //GpHatch * gbr;
          gp(GdipCreateHatchBrush(controls->ghatch, fg, 0, &gbr));
        }
        else if (controls->pattern != 1) {
          // pattern fill: map pen style pattern to hatch brush
          enum HatchStyle hatch;
          switch (controls->pattern) {
            when 2: hatch = HatchStyleDarkHorizontal;
            when 3: hatch = HatchStyleDarkHorizontal;
            when 4: hatch = HatchStyleLightHorizontal;
            when 5: hatch = HatchStyleDarkHorizontal;
            when 6: hatch = HatchStyleNarrowHorizontal;
            when 7: hatch = HatchStyleNarrowHorizontal;
            when 8: hatch = HatchStyleLightHorizontal;
            when 9: hatch = HatchStyleNarrowHorizontal;
            otherwise: hatch = HatchStyle10Percent;
          }
          gp(GdipCreateHatchBrush(hatch, fg, 0, &gbr));
        }
        else {
          //GpSolidFill * gbr;
          gp(GdipCreateSolidFill(fg, &gbr));
        }
        gp(GdipFillPath(gr, gbr, gpath));
        gp(GdipDeleteBrush(gbr));
      }

      gp(GdipDeletePath(gpath));
      gpath = 0;
    }
  }

#endif


  bool temporary = false;


  char * regis_chunk(bool fill, short cmd, struct write_controls * controls, char * r, char fini) {
    // since always controls == &write_controls, we could save this parameter
    //printf("[43;30;2mregis_chunk fill %d %04X[m\n", fill, cmd);

    void flush_shaded(void) {
      // filled paths are already rendered right after F(),
      // flushing them here again would spoil the result
      if (!fill && controls->shading) {
#ifdef use_gdiplus
        // shade the path
        fill_gpath(controls, fill);
#endif
      }
    }

    void reset_temporary_write_controls(void) {
      // if there is a pending shaded object, 
      // flush it before temporary write controls get changed
      flush_shaded();

      // temporary write controls within F() is suppressed 
      // in order to apply to the F() scope, 
      // it is then reset also after the F() section
      if (!fill) {
        temporary = false;
        // reset current controls to saved persistent controls
        write_controls = store_write_controls;
      }
    }

    static struct text_controls sav_txt_ctrl = {1, 0, 0};

    void save_text_controls(void) { sav_txt_ctrl = controls->text; }
    void restore_text_controls(void) { controls->text = sav_txt_ctrl; }


    void regis_define_macro(char let, char * begin, char * end)
    {
      int mi = toupper(let) - 'A';
      if (macro[mi].macro)
        clear_macro(mi);
      // as macros persist between ReGIS graphics, we need to clone them;
      // we could clip the macro but that might be quite complicated...
#if CYGWIN_VERSION_API_MINOR >= 74
      macro[mi].macro = strndup(begin, end - begin);
#else
      (void)begin; (void)end;
#endif
    }
    char * regis_invoke_macro(char * r)
    {
      char let = toupper(*r);
      int mi = let - 'A';
      if (macro[mi].macro && !macro[mi].invoked) {
        macro[mi].invoked = true;  // prevent macro recursion
#ifdef invoke_macros_recursively
        // this way of recursive macro invocation is more elegant
        // but it supports only putting complete commands into macros
        regis_chunk(fill, 0, controls, macro[mi].macro, 0);
        macro[mi].invoked = false;
#else
        // macro invocation by string position push/pop
        // shall support macros anywhere, e.g.
        // @:P [200,400] @; @P
        // and even
        // @:X 200 @; P[@X]
        // as suggested in several ReGIS descriptions:
        // FUNDAMENTALS OF THE REMOTE GRAPHICS INSTRUCTION SET, DEC/TR-95 1979:
        //	"a macrograph string reference causes the characters 
        //	previously defined for that macrograph to be substituted 
        //	for the string reference characters. ... a macrograph string 
        //	may be just an argument of an instruction ..."
        //	"This sequence [@<letter>] may appear anyplace in a 
        //	sequence of REGIS istructions."
        // GIGI/ReGIS Handbook, DEC AA-K336A-TK 1981:
        //	macrograph defines a string that "is a part of 
        //	or a whole ReGIS command string"
        //	and "you can invoke it anywhere in a ReGIS command stream"
        // VT125 GRAPHICS TERMINAL USER GUIDE, DEC EK-VT125-UG-002 1982:
        //	"... command strings or any other string of characters ...
        //	substituted in another command string."
        //	"ReGIS inserts the contents of the macrograph in the command 
        //	string at the position where the macrograph is invoked."
        macro_stack[macro_stacki].mi = mi;
        r++;
#ifdef debug_macros
        printf("invoke %c: ", let);
        println (macro[mi].macro);
        printf("push [%d]: %p ", macro_stacki, r);
        println (r);
#endif
        macro_stack[macro_stacki].ret = r;
        macro_stacki ++;
        return macro[mi].macro;
#endif
      }
      return r;
    }
    char * return_from_macro(char * r)
    {
      // check if this is a macro invocation
      if (macro_stacki) {
        macro_stacki --;
#ifdef debug_macros
        printf("pop  [%d]: %p ", macro_stacki, macro_stack[macro_stacki].ret);
        println (macro_stack[macro_stacki].ret);
#endif
        macro[macro_stack[macro_stacki].mi].invoked = false;
        return macro_stack[macro_stacki].ret;
      }
      return r;
    }
    void regis_clear_macro(char let) {
      clear_macro(toupper(let) - 'A');
    }


    char *
    skip_space(char * s)
    {
      while (*s && *s <= ' ')
        s ++;
      if (*s == '@') {
        char * t = s;
        t++;
        switch (*t) {
          when 'A' ... 'Z' or 'a' ... 'z':
            s = regis_invoke_macro(t);
            s = skip_space(s);
            return s;
        }
      }
      if (!*s) {
        // check if we should return from a macro
        return return_from_macro(s);
      }
      return s;
    }

    int
    scannum1(char * * s)
    {
      *s = skip_space(*s);
      float num = -1.0;
      int len;
      int ret = sscanf(*s, "%f%n", &num, &len);
      if (ret)
        *s += len;
      return roundf(num);
    }

    int
    scannum(char * * s)
    {
      *s = skip_space(*s);
      float num = 0.0;
      int len;
      int ret = sscanf(*s, "%f%n", &num, &len);
      if (ret)
        *s += len;
      return roundf(num);
    }

    void
    scanxy(char * * s)
    {
      int scannat(char * * s)
      {
        switch (**s) {
          when '0' ... '9' or '.':
            return scannum(s);
          when '-' or '+':
            // swallow wrong syntax like T[+50,-50]
            (void)scannum(s);
        }
        return 0;
      }

      (*s) ++;
      *s = skip_space(*s);
      scan_rx = scannat(s);
      *s = skip_space(*s);
      if (**s == ',') {
        (*s) ++;
        *s = skip_space(*s);
        scan_ry = scannat(s);
        *s = skip_space(*s);
      }
    }

    void
    scancoord(char * * s)
    {
      int scanord(int ord, char * * s)
      {
        switch (**s) {
          when '0' ... '9' or '.':
            return scannum(s);
          when '-':
            return ord + scannum(s);
          when '+':
            (*s) ++;
            return ord + scannum(s);
        }
        return ord;
      }

      (*s) ++;
      *s = skip_space(*s);
      new_rx = scanord(curr_rx, s);
      *s = skip_space(*s);
      if (**s == ',') {
        (*s) ++;
        *s = skip_space(*s);
        new_ry = scanord(curr_ry, s);
        *s = skip_space(*s);
      }
      else
        new_ry = curr_ry;
    }


    void screen_erase() {
#ifdef use_gdiplus
      COLORREF bg = controls->background;
      //bg = RGB(0, 0, 200);
      ARGB gbg = GpARGB(red(bg), green(bg), blue(bg));
      GpSolidFill * gbr;
      gp(GdipCreateSolidFill(gbg, &gbr));
      gp(GdipFillRectangle(gr, gbr, 0, 0, rwidth, rheight));
      gp(GdipDeleteBrush(gbr));
      //gp(GdipFlush(gr, FlushIntentionFlush));
#endif
      if (cfg.regis_grid)
        screen_grid(cfg.regis_grid);
    }


    void coordinates(unsigned short coordcmd, flint rx, flint ry) {
      //printf("[] <%c(%c) %02X(%02X)> %d/%d->%d/%d\n", coordcmd >> 8, coordcmd & 0xFF, coordcmd >> 8, coordcmd & 0xFF, (int)curr_rx, (int)curr_ry, (int)rx, (int)ry);
      bool filling = fill || controls->shading;

      char cmd = coordcmd >> 8;
      coordcmd &= 0xFF;
      bool stack_only = cmd == 'P' || cmd == 'V';
      float x = rx * scale;
      float y = ry * scale;
      static float currx = 0;
      static float curry = 0;
      currx = curr_rx * scale;
      curry = curr_ry * scale;

      static GpPointF curvp[256];
      static uint curvi = 0;

      switch (coordcmd) {
        when 0:  // stop curve interpolation on S(E)
          curvi = 0;
        when 'P':
          P:
          // move position
          curr_rx = rx;
          curr_ry = ry;
        when 'V':
          V:
          //printf(" V %f/%f..%f/%f\n", currx, curry, x, y);
#ifndef use_gdiplus
          // for line drawing, we need to use GDI+ in two cases
          // - to prepare a path to be filled within an F command
          // - if scale >= 2 as GDI dashed lines do not work then
          // otherwise we preferred GDI dashed lines which appear much nicer;
          // changed to preferring GDI+ after implementing custom dash style
          (void)filling;
          //if (!filling && scale < 2.0) {  // && scale < 2.0 ?
            MoveToEx(dc, currx, curry, 0);
            set_pen(dc, controls);
            LineTo(dc, x, y);
            // for solid lines, draw back to include end point
            // (this would spoil appearance of dashed lines)
            if (controls->pattern == 1)
              LineTo(dc, currx, curry);
          //}
          //else
#endif
#ifdef use_gdiplus
          {
            if (filling) {
              init_gpath();
              gp(GdipAddPathLine(gpath, currx, curry, x, y));
            }
            else {
              set_gpen(controls, scale);
              if (x == currx && y == curry)
                // draw dot
                gp(GdipDrawArc(gr, gpen, x - scale / 2, y - scale / 2, scale, scale, 0, 360));
              else
                gp(GdipDrawLine(gr, gpen, currx, curry, x, y));
            }
          }
#endif
          // move position
          curr_rx = rx;
          curr_ry = ry;
        when 'C': { // Curve segment, Circle/Arc
          //printf(" C %f/%f..%f/%f\n", currx, curry, x, y);
          if (posi && posstack[posi - 1].cmd == 'C'
           && interpolating
           && (posstack[posi - 1].f == 'B' || posstack[posi - 1].f == 'S'))
          {
            // curve segment after C(B) or C(S)
            if (curvi < lengthof(curvp)) {
              curvp[curvi].X = x;
              curvp[curvi].Y = y;
              curvi ++;
            }
            // move position
            curr_rx = rx;
            curr_ry = ry;
          }
          else {  // Circle/Arc
            flint cx, cy, ux, uy;  // center / circumference position
            bool move_position = false;

            if (centerspec) {
              // Circle/Arc with Center at Specified Position
              cx = x;
              cy = y;
              ux = currx;
              uy = curry;
              if (angle) {
                // Arc: update cursor to arc end position
                move_position = true;
              }
            }
            else {
              // Circle/Arc with Center at Current Position
              cx = currx;
              cy = curry;
              ux = x;
              uy = y;
            }

            // bounding box of circle
            flint dx = ux - cx;
            flint dy = uy - cy;
            float r = sqrtf(dx * dx + dy * dy);
            flint d = 2 * r;
            flint x0 = cx - r;
            flint y0 = cy - r;
            // start and sweep angles for rendering (GDI+)
            // for angle calculation note:
            // - the ReGIS sweep angle goes counter-clockwise
            // - vertical screen coordinates go downwards
            // - GDI+ angles start at right and go clockwise
            float start = 0;  // from x axis: 0 right of center, 90° bottom
            float sweep = 360;
            if (angle && r) {
              start = atan2f(uy - cy, ux - cx) / 2 * 360.0 / M_PI;
              sweep = - angle;
            }
            //printf(" circle/arc center %d/%d circumference point %d/%d radius %f bbox %d/%d angle %f..%f move %d\n", cx, cy, ux, uy, r, x0, y0, start, sweep, move_position);

            if (move_position) {
              // calculate Arc end position, in ReGIS coordinates
              float end = start - angle;
              float endrad = end * M_PI / 180.0;
              currx = cx + r * cosf(endrad);
              curry = cy + r * sinf(endrad);

              // scale back to ReGIS coordinates:
              // rounding here helps smooth out edges 
              // which appear as transition artefacts when combining 
              // lines and arcs to a (filled) path;
              // surprisingly this helps even if type flint is float
              curr_rx = roundf(currx / scale);
              curr_ry = roundf(curry / scale);
            }

#ifdef use_gdiplus
            if (filling) {
              init_gpath();
              gp(GdipAddPathArc(gpath, x0, y0, d, d, start, sweep));
            }
            else {
              set_gpen(controls, scale);
              gp(GdipDrawArc(gr, gpen, x0, y0, d, d, start, sweep));
            }
#else
            // calculate Arc end position, in ReGIS coordinates
            float end = start + sweep;
            float endrad = end * M_PI / 180.0;
            flint endx = cx + r * cosf(endrad);
            flint endy = cy + r * sinf(endrad);
            //printf("Arc center %d/%d outer %d/%d sweep %f\n", cx, cy, ux, uy, sweep);
            //printf("- rect %d/%d %d/%d end %d/%d\n", x0, y0, x0 + d, y0 + d, endx, endy);
            set_pen(dc, controls);
            SetArcDirection(dc, sweep > 0 ? AD_CLOCKWISE : AD_COUNTERCLOCKWISE);
            Arc(dc, x0, y0, x0 + d, y0 + d, ux, uy, endx, endy);
#endif
          }  // end else // Circle/Arc
        }
        when 'S':  // push Unbounded Position / Start Open Curve
          //printf(" S stack_only %d\n", stack_only);
          if (posi < stacklen - 1) {
            posstack[posi].cmd = cmd;
            posstack[posi].f = 'S';
            posstack[posi].rx = curr_rx;
            posstack[posi].ry = curr_ry;
            posi ++;
          }
          if (stack_only)
            break;

          interpolating = true;
          // add curve point only if this is a C(S)
          curvp[0].X = x;
          curvp[0].Y = y;
          curvi = 1;
        when 'B':  // push Bounded Position / Begin Closed Curve
          //printf(" B stack_only %d\n", stack_only);
          if (posi < stacklen - 1) {
            posstack[posi].cmd = cmd;
            posstack[posi].f = 'B';
            posstack[posi].rx = curr_rx;
            posstack[posi].ry = curr_ry;
            posi ++;
          }
          if (stack_only)
            break;

          interpolating = true;
          // add curve point only if this is a C(B)
          curvp[0].X = x;
          curvp[0].Y = y;
          curvi = 1;
        when 'E': {  // End Curve; return to (B) position if Bounded
          bool closed = false;
          if (posi) {
            posi --;
            if (posstack[posi].f == 'B') {
              closed = true;
              // Bounded Position Stack: return to previous position
              curr_rx = posstack[posi].rx;
              curr_ry = posstack[posi].ry;
              // reset target coordinates to (new) current position
              rx = curr_rx;
              ry = curr_ry;
              // scale popped ReGIS position
              x = rx * scale;
              y = ry * scale;
            }
          }
          //printf(" E closed %d\n", closed);
          if (cmd == 'V') {
            goto V;  // perform V command on popped position
          }
          if (cmd == 'P') {
            goto P;  // perform P command on popped position
          }
          //if (stack_only)
          //  break;  // done by goto

#ifdef use_gdiplus
          // finish/close curve only if this is a C(E)
          if (closed) {
            if (filling) {
              init_gpath();
              // add curve to path
              gp(GdipAddPathClosedCurve2(gpath, curvp, curvi, tension));
            }
            else {
              set_gpen(controls, scale);
              gp(GdipDrawClosedCurve2(gr, gpen, curvp, curvi, tension));
            }
          }
          else {
            if (filling) {
              init_gpath();
              // add curve to path
              gp(GdipAddPathCurve3(gpath, curvp, curvi, 1, curvi - 3, tension));
            }
            else {
              set_gpen(controls, scale);
              gp(GdipDrawCurve3(gr, gpen, curvp, curvi, 1, curvi - 3, tension));
            }
          }
#else
          (void)closed;
#endif

          // flush curve points
          curvi = 0;
        }
      }
      //printf("coord -> [%d/%d]\n", curr_rx, curr_ry);
    }


    char rstate = 0;

    // loop the chunk
    while (*r) {
      //printf(" %c (fini %02X) f %c cmd %02X\n", *r, fini, rstate, cmd);

      // return at end of chunk
      if (*r == fini) {
        r++;
        break;
      }
      // skip comment lines
      if (*r == '\n') {
        r++;
#if 0
        if (*r == '#') {
#ifdef debug_regis
          println(r);
#endif
          while (*r && *r != '\n')
            r++;
          continue;
        }
#endif
      }
      // remember current position for proper loop termination
      char * last_r = r;

      // check command, sub-command, or syntax character
      char let = toupper(*r);
      // Options and suboptions
      if (cmd == 'W')  // Write Control
        switch (let) {
          when 'M':  // PV Multiplication
            r++;
            controls->PV = scannum(&r);
            //continue;  // before ')', to break the loop
          when 'P':  // Pattern Control
            r++;
            int pattern = scannum1(&r);
            if (pattern >= 0)
              controls->pattern = pattern;
            r = skip_space(r);
            if (*r == '(')
              r = regis_chunk(fill, subcmd('W', 'P'), controls, r + 1, ')');
            //continue;  // before ')', to break the loop
          when 'I':  // Foreground Intensity
            r++;
            r = skip_space(r);
            if (*r == '(')
              r = regis_chunk(fill, subcmd('W', 'I'), controls, r + 1, ')');
            else if (*r >= '0' && *r <= '9') {
              int coli = scannum(&r);
              if (coli <= 15)
                controls->foreground = mapcol(coli);
            }
            //continue;  // before ')', to break the loop
          when 'F':  // Plane Select
            r++;
            controls->plane = scannum(&r);
            //continue;  // before ')', to break the loop
          when 'V' or 'R' or 'C' or 'E':  // Writing Style
            controls->writing_style = let;
          when 'N':  // Negative Pattern Control
            r++;
            controls->negative = scannum(&r);
            //continue;  // before ')', to break the loop
          when 'S':  // Shading Control - what a mess
            r++;
            r = skip_space(r);
            int shade_x = 0;
            if (*r == '(') {
              r++;
              r = skip_space(r);
              if (toupper(*r) == 'X') {
                shade_x = 1;
                r++;
                r = skip_space(r);
              }
              if (*r == ')') {
                r++;
                r = skip_space(r);
              }
            }
            controls->shade_vert = shade_x;

            if (*r >= '0' && *r <= '9') {
              int shading = scannum(&r);
              if (controls->shading && !shading) {
#ifdef use_gdiplus
                // shade the path: flush if shading got switched off
                fill_gpath(controls, fill);
#endif
              }
              controls->shading = shading;
              controls->shade_vert = false;
              controls->shade_x = curr_rx;
              controls->shade_y = curr_ry;
              r = skip_space(r);
            }

            if (*r == '[') {
              scancoord(&r);  // &new_rx, &new_ry
              // shading reference line
              controls->shade_x = new_rx;
              controls->shade_y = new_ry;
              controls->shading = 1;
            }
            else if (*r == '"' || *r == '\'') {
              wchar * text;
              r = regis_string(r, &text);
              if (text) {
                // enable shading
                controls->shading = *text;
                controls->shade_x = curr_rx;
                controls->shade_y = curr_ry;
                // determine hatch pattern
                controls->hatch = hatch(*text);
                controls->ghatch = ghatch(*text);
                free(text);
              }
            }
        }
      else if (cmd == subcmd('W', 'P'))  // Pattern Control suboption
        switch (let) {
          when 'M':  // Pattern Multiplication
            r++;
            controls->patternM = scannum(&r);
            //continue;  // before ')', to break the loop
        }
      else if (cmd == subcmd('W', 'I'))  // Foreground Intensity suboption
        switch (let) {
          when 'D':  // dark (black)
            controls->foreground = RGB(0, 0, 0);
          when 'R':  // red
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setred(&controls->foreground, scannum(&r));
            else
              controls->foreground = RGB(255, 0, 0);
          when 'G':  // green
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setgreen(&controls->foreground, scannum(&r));
            else
              controls->foreground = RGB(0, 255, 0);
          when 'B':  // blue
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setblue(&controls->foreground, scannum(&r));
            else
              controls->foreground = RGB(0, 0, 255);
          when 'C':  // cyan
            controls->foreground = RGB(0, 255, 255);
          when 'Y':  // yellow
            controls->foreground = RGB(255, 255, 0);
          when 'M':  // magenta
            controls->foreground = RGB(255, 0, 255);
          when 'W':  // white
            controls->foreground = RGB(255, 255, 255);
          when 'H':  // hue
            r++;
            sethue(&controls->foreground, scannum(&r));
            //continue;  // before ')', to break the loop
          when 'L':  // lightness
            r++;
            setlightness(&controls->foreground, scannum(&r));
            //continue;  // before ')', to break the loop
          when 'S':  // saturation
            r++;
            setsaturation(&controls->foreground, scannum(&r));
            //continue;  // before ')', to break the loop
        }
      else if (cmd == 'S')  // Screen Control
        switch (let) {
          when 'I':  // Background Intensity
            r++;
            r = skip_space(r);
            if (*r == '(')
              r = regis_chunk(fill, subcmd('S', 'I'), controls, r + 1, ')');
            else if (*r >= '0' && *r <= '9') {
              int coli = scannum(&r);
              if (coli <= 15)
                controls->background = mapcol(coli);
            }
            //continue;  // before ')', to break the loop
          when 'E':  // Screen Erase
            // set screen to display background
            screen_erase();
            // reset Write Control shading
            controls->shading = 0;
            // stop curve interpolation
            coordinates(0, 0, 0);
            // clear position stacks
            posi = 0;
            // do not change background colour or shade
            // do not change cursor position
          when 'T': { // Time Delay
            r++;
            int ticks = scannum(&r);  // 1/60 seconds
#ifdef use_gdiplus
            // reduce window stalling on long delays:
            gp(GdipFlush(gr, FlushIntentionFlush));  // not strictly needed
#endif
            // just flushing graphics here does not work as we're 
            // rendering on a temporary DC;
            // so we need to invoke a flushing callback
            flush();

            // the actual delay
            if (ticks >= 0 && ticks <= 32767) {
              long us = ticks * 1000000 / 60;
              us /= 4;  // reproduce xterm acceleration
              usleep(us);
            }
          }
#ifdef support_screen_coordinates_control
          when 'W':  // Write Control, for PV multiplier only
            // e.g. S(W(M15))6
            r = skip_space(r);
            if (*r == '(')
              r = regis_chunk(fill, subcmd('S', 'W'), controls, r + 1, ')');
#endif
          // other Screen Controls not implemented
        }
#ifdef support_screen_coordinates_control
      else if (cmd == subcmd('S', 'W'))  // Write Control suboption
        switch (let) {
          when 'M':  // PV multiplier for Screen scrolling
            r++;
            controls->PV = scannum(&r);  // only useful for offset scrolling
        }
#endif
      else if (cmd == subcmd('S', 'I'))  // Background Intensity suboption
        switch (let) {
          when 'D':  // dark (black)
            controls->background = RGB(0, 0, 0);
          when 'R':  // red
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setred(&controls->background, scannum(&r));
            else
              controls->background = RGB(255, 0, 0);
          when 'G':  // green
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setgreen(&controls->background, scannum(&r));
            else
              controls->background = RGB(0, 255, 0);
          when 'B':  // blue
            r++;
            r = skip_space(r);
            if (*r >= '0' && *r <= '9')
              setblue(&controls->background, scannum(&r));
            else
              controls->background = RGB(0, 0, 255);
          when 'C':  // cyan
            controls->background = RGB(0, 255, 255);
          when 'Y':  // yellow
            controls->background = RGB(255, 255, 0);
          when 'M':  // magenta
            controls->background = RGB(255, 0, 255);
          when 'W':  // white
            controls->background = RGB(255, 255, 255);
          when 'H':  // hue
            r++;
            sethue(&controls->background, scannum(&r));
            //continue;  // before ')', to break the loop
          when 'L':  // lightness
            r++;
            setlightness(&controls->background, scannum(&r));
            //continue;  // before ')', to break the loop
          when 'S':  // saturation
            r++;
            setsaturation(&controls->background, scannum(&r));
            //continue;  // before ')', to break the loop
        }
      else if (cmd == 'C' && let == 'S') {  // Start Open Curve
        coordinates(subcmd('C', 'S'), curr_rx, curr_ry);
      }
      else if (cmd == 'C' && let == 'B') {  // Begin Closed Curve
        coordinates(subcmd('C', 'B'), curr_rx, curr_ry);
      }
      else if (cmd == 'C' && let == 'E') {  // End Curve
        coordinates(subcmd('C', 'E'), curr_rx, curr_ry);
      }
      else if (cmd == 'P' && let == 'S') {  // Start Position Stack
        coordinates(subcmd('P', 'S'), curr_rx, curr_ry);
      }
      else if (cmd == 'P' && let == 'B') {  // Begin Position Stack
        coordinates(subcmd('P', 'B'), curr_rx, curr_ry);
      }
      else if (cmd == 'P' && let == 'E') {  // End Position Stack
        coordinates(subcmd('P', 'E'), curr_rx, curr_ry);
      }
      else if (cmd == 'V' && let == 'S') {  // Start Position Stack
        coordinates(subcmd('V', 'S'), curr_rx, curr_ry);
      }
      else if (cmd == 'V' && let == 'B') {  // Begin Position Stack
        coordinates(subcmd('V', 'B'), curr_rx, curr_ry);
      }
      else if (cmd == 'V' && let == 'E') {  // End Position Stack
        coordinates(subcmd('V', 'E'), curr_rx, curr_ry);
      }
      else if (cmd == 'C' && let == 'C') {  // Arc mode
        centerspec = true;
      }
      else if (cmd == 'C' && let == 'A') {  // Arc angle
        r ++;
        angle = scannum(&r);
        //continue;
      }
      else if (cmd == 'T')  // Text Options
        switch (let) {
          when 'S':  // Standard Character Cell Size
            r ++;
            int size = scannum1(&r);
            if (size >= 0 && size <= 16)
              controls->text.size = size;
          when 'D':  // String Tilt, in 45° steps, 90° is upwards
            r ++;
            // TODO: check second tilt option (Character Tilt)?
            int tilt = scannum(&r);
            controls->text.tilt = tilt;
            // Note: DEC describes a combination of tilt with a size option;
            // a single tilt option without subsequent size option 
            // is interpreted as Character Tilt by xterm
          when 'I':  // Italics Option
            r ++;
            int it = scannum(&r);
            controls->text.italic = it < 0;
          when 'B':  // switch to Temporary Text Control
            save_text_controls();
          when 'E':  // switch to common Text Control
            restore_text_controls();
          when 'W':  // Temporary Write Controls
            temporary = true;
            r = regis_chunk(fill, 'W', controls, r + 1, ')');
        }
      else if (!(cmd & 0xFF00)) {
        // Commands
        switch (let) {
          when '@':  //
            r ++;
            switch (*r) {
              when ':': { // define macrograph
                r ++;
                let = *r;
                if (let >= 'A' && let <= 'Z') {
                  r ++;
                  if (*r == '@' && *(r + 1) == ';') {
                    // @:X@; clear macrograph
                    regis_clear_macro(let);
                    r ++;
                  }
                  else {
                    // @:X...@; define macrograph (@; expected)
                    char * m = r;
                    while (*r) {
                      if (0 == strncmp("@;", r, 2)) {
                        // store macro definition, from m to here
                        regis_define_macro(let, m, r);
                        r ++;
                        break;
                      }
                      else if (*r == '"' || *r == '\'')
                        r = regis_string(r, 0);
                      else
                        r ++;
                      }
                    }
                }
                else {
                  // ignore and continue processing, behaviour undefined
                }
              }
              when '.':  // clear all macrographs
                for (char c = 'A'; c <= 'Z'; c++) 
                  regis_clear_macro(c);
                r++;
              when 'A' ... 'Z' or 'a' ... 'z':
                r = regis_invoke_macro(r);
            }
          when '"' or '\'':
            if (rstate == 'T') {
              // read text
              wchar * text = 0;
              r = regis_string(r, &text);
              if (text) {
                // output text
                regis_text(dc, scale, controls, text);
                free(text);
              }
            }
            else
              // skip stray text string (for example used as comment after ;)
              r = regis_string(r, 0);
            //continue;
          when '(':
            if (rstate == 'W' && !cmd)
              // not invoked as W() is actively parsed below
              r = regis_chunk(fill, rstate, controls, r + 1, ')');
            else if (rstate == 'F')
              // not invoked as F() is actively parsed below
              r = regis_chunk(fill, 0, controls, r + 1, ')');
            else if (rstate == 'T')
              // parsing T options
              r = regis_chunk(fill, rstate, controls, r + 1, ')');
            else
              // used for P, V, C, S
              r = regis_chunk(fill, rstate, controls, r + 1, ')');
            //continue;  // before ')', to break the loop
          when '[':
            if (strchr("PVC", rstate)) {
              scancoord(&r);  // &new_rx, &new_ry
              if (*r == ']')
                switch (rstate) {
                  when 'P': coordinates('P', new_rx, new_ry);
                  when 'V': coordinates('V', new_rx, new_ry);
                  when 'C': coordinates('C', new_rx, new_ry);
                }
            }
            else {
              scanxy(&r);  // swallow stray [...] interval
            }
            //continue;
          when '0' ... '9' or '.':
            if (rstate == 'T')  // Text PV Spacing
              switch (let) {
                when '0':  // move forward half width
                           regis_text(0, scale, controls, (void*)0);
                when '4':  // move backward half width (44 overstrike)
                           regis_text(0, scale, controls, (void*)4);
                when '6':  // move down (subscript)
                           regis_text(0, scale, controls, (void*)6);
                when '7':  // move down (subscript) and half spacing
                           regis_text(0, scale, controls, (void*)7);
                when '2':  // move up (superscript)
                           regis_text(0, scale, controls, (void*)2);
                when '1':  // move up (superscript) and half spacing
                           regis_text(0, scale, controls, (void*)1);
                when '3':  // move up and half width back
                           regis_text(0, scale, controls, (void*)3);
                when '5':  // move down and half width back
                           regis_text(0, scale, controls, (void*)5);
              }
            else if (strchr("PVC", rstate)) {
              int dx = 0, dy = 0;
              int PV = controls->PV;
              switch (*r) {
                when '0': dx = PV;
                when '1': dx = PV; dy = -PV;
                when '2': dy = -PV;
                when '3': dy = -PV; dx = -PV;
                when '4': dx = -PV;
                when '5': dx = -PV; dy = PV;
                when '6': dy = PV;
                when '7': dy = PV; dx = PV;
              }
              if (dx || dy) {
                switch (rstate) {
                  when 'P': coordinates('P', curr_rx + dx, curr_ry + dy);
                  when 'V': coordinates('V', curr_rx + dx, curr_ry + dy);
                  when 'C': coordinates('C', curr_rx + dx, curr_ry + dy);
                }
              }
            }
          when 'P':
            flush_shaded();
            rstate = let;
          when 'V':
            reset_temporary_write_controls();
            rstate = let;
          when 'C':  // Curve
            interpolating = false;
            centerspec = false;
            angle = 0;
            reset_temporary_write_controls();
            rstate = let;
          when 'F':  // Fill
            reset_temporary_write_controls();
            r++;
            r = skip_space(r);
            if (*r == '(')
              r = regis_chunk(true, 0, controls, r + 1, ')');
#ifdef use_gdiplus
            // draw the filled path
            fill_gpath(controls, fill);
#endif
            // temporary write controls within F() is suppressed 
            // in order to apply to the F() scope, 
            // so we need to reset it after the F() section
            reset_temporary_write_controls();
            //continue;  // before ')', to break the loop
          when 'T':  // Text
            reset_temporary_write_controls();
            rstate = let;
          when 'S':  // Screen Control
            reset_temporary_write_controls();
            rstate = let;
          when 'W': {  // Write Control
            temporary = cmd || fill;
            bool setting_temporary_write_controls = temporary;
            reset_temporary_write_controls();
            rstate = let;
            // process write controls, store into current controls
            r++;
            r = skip_space(r);
            if (*r == '(') {
              r = regis_chunk(fill, 'W', controls, r + 1, ')');
            }

            if (!setting_temporary_write_controls) {
              // save to persistent write controls for later reset
              store_write_controls = write_controls;
            }
            //continue;  // before ')', to break the loop
          }
          when 'L' or 'R':  // Load character set / Report - ignored
            rstate = let;
          when ';':  // resynchronization command
#ifdef debug_regis
            println(r);
#endif
            reset_temporary_write_controls();
            rstate = let;
          when '#':  // comment with log output
#ifdef debug_regis
            println(r);
#endif
            while (*r && *r != '\n')
              r++;
        }
      }

      // advance pointer in ReGIS program if needed
      if (r == last_r && *r && *r != fini && *r != '\n')
        r ++;

      if (!*r) {
        // check if we should return from a macro
        r = return_from_macro(r);
      }
    }

    return r;
  }

  regis_chunk(false, 0, &write_controls, (char *)regis, 0);

  if (write_controls.shading) {
#ifdef use_gdiplus
    // flush pending path shading
    fill_gpath(&write_controls, false);
#endif
  }

#ifdef use_gdiplus
  if (gpen) {
    gp(GdipDeletePen(gpen));
    gpen = 0;
  }
  gp(GdipFlush(gr, FlushIntentionFlush));
  gp(GdipDeleteGraphics(gr));
#endif
}
