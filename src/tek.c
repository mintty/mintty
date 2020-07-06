#include "tek.h"

enum tekmode tek_mode = TEKMODE_OFF;
bool tek_bypass = false;
static uchar intensity = 0x7D; // for point modes
static uchar style = 0;        // for vector modes
static uchar font = 0;
static bool beam_defocused = false;
static bool plotpen = false;
static bool apl_mode = false;

static short tek_y, tek_x;
static uchar lastfont = 0;

static int beam_glow = 0;

static wchar * APL = W(" ¨)<≤=>]∨∧≠÷,+./0123456789([;×:\\¯⍺⊥∩⌊∊_∇∆⍳∘'⎕∣⊤○⋆?⍴⌈∼↓∪ω⊃↑⊂←⊢→≥-⋄ABCDEFGHIJKLMNOPQRSTUVWXYZ{⊣}$ ");

struct tekfont {
  void * f;
  short rows, cols;
  short hei, wid;
} tekfonts[] = {
  {0, 35, 74, 87, 55},
  {0, 38, 81, 80, 50},
  {0, 58, 121, 52, 33},
  {0, 64, 133, 48, 30}
};

static short margin = 0;

struct tekchar {
  char type;
  uchar recent;
  bool defocused;
#if CYGWIN_VERSION_API_MINOR >= 74
  union {
    struct {
      wchar c;
      short w;
      uchar font;
    };
    struct {
      short y, x;
      uchar intensity;
      uchar style;
    };
  };
#else
  wchar c;
  short w;
  short y, x;
  short intensity;
#endif
};
static struct tekchar * tek_buf = 0;
static int tek_buf_len = 0;


static void
tek_home(void)
{
  tek_x = 0;
  tek_y = 3120 - tekfonts[font].hei;
  margin = 0;
}

void
tek_clear(void)
{
  if (tek_buf)
    free(tek_buf);
  tek_buf = 0;
  tek_buf_len = 0;
  tek_home();
}

void
tek_reset(void)
{
  intensity = 0x7D;
  style = 0;
  font = 0;
  tek_clear();
}

void
tek_font(short f)
{
  font = f & 3;
}

void
tek_write(wchar c, int width)
{
  if (tek_bypass)
    return;

  if (apl_mode && c >= ' ' && c < 0x80) {
    c = APL[c - ' '];
    width = 1;
  }

  tek_buf = renewn(tek_buf, tek_buf_len + 1);
  tek_buf[tek_buf_len ++] = (struct tekchar)
                            {.type = 0, .recent = beam_glow,
                             .defocused = beam_defocused,
                             .c = c, .w = width, .font = font};
  if (width > 0) {
    tek_x += width * tekfonts[font].wid;
  }
}

void
tek_alt(bool alt_chars)
{
  apl_mode = alt_chars;
}


void
tek_copy(void)
{
}


/* vector_style
   0 solid
   1 dotted
   2 dot-dashed
   3 short dashed
   4 long dashed
*/
void
tek_beam(bool defocused, bool write_through, char vector_style)
{
  (void)write_through;
  beam_defocused = defocused;
  if (vector_style > 4)
    style = 0;
  else
    style = vector_style;
}

void
tek_intensity(bool defocused, int i)
{
  beam_defocused = defocused;
  intensity = i;
}

#define dont_debug_graph

void
tek_address(char * code)
{
#ifdef debug_graph
  printf("tek_address %d <%s>", tek_mode, code);
#endif
  /* https://vt100.net/docs/vt3xx-gp/chapter13.html#S13.14.3
	tag bits
	https://vt100.net/docs/vt3xx-gp/chapter13.html#T13-4
	01 11 11 01 10	12 Address Bits
			01	High Y
			11	Extra
			11	Low Y
			01	High X
			10	Low X
	01 11 01 10	10 Address Bits
	https://vt100.net/docs/vt3xx-gp/chapter13.html#T13-5
	01 10		High Y	Low X
	11 10		Low Y	Low X
	11 01 10	Low Y	High X	Low X
	10		Low X
	11 11 10	Extra	Low Y	Low X
	more seen
	01 11 10	High Y	Low Y	Low X
	11 11 01 10	Extra	Low Y	High X	Low X
	01 11 11 10	High Y	Extra	Low Y	Low X
	more unseen
	01 01 10	High Y	High X	Low X
  */
  // accumulate tags for switching; clear tags from input
  short tag = 0;
  char * tc = code;
  while (* tc) {
    tag = (tag << 2) | (* tc >> 5);
#ifdef debug_graph
    printf(" %d%d", *tc >> 6, (*tc >> 5) & 1);
#endif
    * tc &= 0x1F;
    tc ++;
  }
#ifdef debug_graph
  printf("\n");
#endif

  // switch by accumulated tag sequence
  if (tag == 0x1F6) {      // 12 Bit Address
    tek_y = code[0] << 7 | code[2] << 2 | code[1] >> 2;
    tek_x = code[3] << 7 | code[4] << 2 | (code[1] & 3);
  }
  else if (tag == 0x76) {  // 10 Bit Address
    tek_y = code[0] << 7 | code[1] << 2;
    tek_x = code[2] << 7 | code[3] << 2;
  }
  else if (tag == 0x06) {  // Short Address, Byte Changed: High Y
    //	01 10		High Y	Low X
    tek_y = (tek_y & 0x7F) | code[0] << 7;
    tek_x = (tek_x & 0xF83) | code[1] << 2;
  }
  else if (tag == 0x0E) {  // Short Address, Byte Changed: Low Y
    //	11 10		Low Y	Low X
    tek_y = (tek_y & 0xF83) | code[0] << 2;
    tek_x = (tek_x & 0xF83) | code[1] << 2;
  }
  else if (tag == 0x36) {  // Short Address, Byte Changed: High X
    //	11 01 10	Low Y	High X	Low X
    tek_y = (tek_y & 0xF83) | code[0] << 2;
    tek_x = (tek_x & 0x3) | code[1] << 7 | code[2] << 2;
  }
  else if (tag == 0x02) {  // Short Address, Byte Changed: Low X
    //	10		Low X
    tek_x = (tek_x & 0xF83) | code[0] << 2;
  }
  else if (tag == 0x3E) {  // Short Address, Byte Changed: Extra
    //	11 11 10	Extra	Low Y	Low X
    tek_y = (tek_y & 0xF80) | code[1] << 2 | code[0] >> 2;
    tek_x = (tek_x & 0xF80) | code[2] << 2 | (code[0] & 3);
  }
  else if (tag == 0x1E) {
    //	01 11 10	High Y	Low Y	Low X
    tek_y = (tek_y & 0x3) | code[0] << 7 | code[1] << 2;
    tek_x = (tek_x & 0xF83) | code[2] << 2;
  }
  else if (tag == 0xF6) {
    //	11 11 01 10	Extra	Low Y	High X	Low X
    tek_y = (tek_y & 0xF80) | code[1] << 2 | code[0] >> 2;
    tek_x = code[2] << 7 | code[3] << 2 | (code[0] & 3);
  }
  else if (tag == 0x7E) {
    //	01 11 11 10	High Y	Extra	Low Y	Low X
    tek_y = code[0] << 7 | code[2] << 2 | code[1] >> 2;
    tek_x = (tek_x & 0xF80) | code[3] << 2 | (code[1] & 3);
  }
  else if (tag == 0x16) {
    //	01 01 10	High Y	High X	Low X
    tek_y = (tek_y & 0x7F) | code[0] << 7;
    tek_x = (tek_x & 0x3) | code[1] << 7 | code[2] << 2;
  }
  else {  // error
#ifdef debug_graph
  printf(" -> err\n");
#endif
    return;
  }

#ifdef debug_graph
  printf(" -> (%d) -> %d:%d\n", tek_mode, tek_y, tek_x);
#endif
  tek_buf = renewn(tek_buf, tek_buf_len + 1);
  tek_buf[tek_buf_len ++] = (struct tekchar) 
    {.type = tek_mode, .recent = beam_glow,
     .defocused = beam_defocused,
     .y = tek_y, .x = tek_x,
     .style = style, .intensity = intensity};
}

/*	DEAIHJBF
	0100	up	DEF
	0001	right	EAI
	1000	down	IHJ
	0010	left	JBF
 */
void
tek_step(char c)
{
  if (c & 8)
    tek_y -= 1;
  if (c & 4)
    tek_y += 1;
  if (c & 2)
    tek_x -= 1;
  if (c & 1)
    tek_x += 1;

  if (plotpen) {
    tek_buf = renewn(tek_buf, tek_buf_len + 1);
    tek_buf[tek_buf_len ++] = (struct tekchar) 
      {.type = TEKMODE_POINT_PLOT, .recent = beam_glow,
       .defocused = beam_defocused,
       .y = tek_y, .x = tek_x, .intensity = intensity};
  }
  else {
    tek_buf = renewn(tek_buf, tek_buf_len + 1);
    tek_buf[tek_buf_len ++] = (struct tekchar) 
      {.type = TEKMODE_GRAPH0, .recent = beam_glow,
       .defocused = beam_defocused,
       .y = tek_y, .x = tek_x, .intensity = 0};
  }
}

void
tek_pen(bool on)
{
  plotpen = on;
  if (on)
    tek_step(0);
}


static void
trace_tek(void)
{
#ifdef debug_tek_output
static bool ptd = false;
  if (ptd) return;

  for (int i = 0; i < tek_buf_len; i++) {
    struct tekchar * tc = &tek_buf[i];

    if (tc->type == TEKMODE_GRAPH0)
      printf("move %4d %4d\n", tc->y, tc->x);
    else if (tc->type == TEKMODE_GRAPH)
      printf("line %4d %4d\n", tc->y, tc->x);
    else if (tc->type == TEKMODE_POINT_PLOT)
      printf("plot %4d %4d\n", tc->y, tc->x);
    else
      printf("text %04X:%d\n", tc->c, tc->w);
  }

  ptd = true;
#endif
}


#include "child.h"

void
tek_send_address(void)
{
  child_printf("%c%c%c%c",
               0x20 | (tek_y >> 7), 0x60 | ((tek_y >> 2) & 0x1F),
               0x20 | (tek_x >> 7), 0x40 | ((tek_x >> 2) & 0x1F));
}

void
tek_enq(void)
{
  child_write("4", 1);
  tek_send_address();
}

#include <windows.h>
#include "winpriv.h"

colour fg;

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

static void
init_font(short f)
{
  if (tekfonts[f].f)
    DeleteObject(tekfonts[f].f);

  wstring fn = *cfg.tek_font ? cfg.tek_font : cfg.font.name;
  tekfonts[f].f = CreateFontW(
                  - tekfonts[f].hei, - tekfonts[f].wid, 
                  0, 0, FW_NORMAL, 0, 0, 0,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  get_font_quality(), FIXED_PITCH | FF_DONTCARE,
                  fn);
}

static void
out_text(HDC dc, short x, short y, wchar * s, uchar f)
{
  SelectObject(dc, tekfonts[f].f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);

  ExtTextOutW(dc, x, y, 0, 0, s, wcslen(s), 0);
}

static short txt_y, txt_x;
static short out_y, out_x;
static wchar * txt = 0;
static int txt_len = 0;
static int txt_wid = 0;

static void
flush_text(HDC dc)
{
  if (txt) {
    static short offset = 0; // 16? but then the bottom will get clipped
    out_text(dc, txt_x, 3120 - txt_y - tekfonts[font].hei + offset, txt, lastfont & 3);
    free(txt);
    txt = 0;
    txt_len = 0;
    txt_wid = 0;
  }
}

static void
write_cr(void)
{
  out_x = margin;
}

static void
write_lf(void)
{
  short ph = tekfonts[lastfont & 3].hei;
  out_y -= ph;
  if (out_y < 0) {
    out_y = 3120 - ph;
    margin = 2048 - margin;
    out_x = (out_x + 2048) % 4096;
  }
}

static void
write_up(void)
{
  short ph = tekfonts[lastfont & 3].hei;
  out_y += ph;
  if (out_y + ph >= 3120) {
    out_y = 0;
    margin = 2048 - margin;
    out_x = (out_x + 2048) % 4096;
  }
}

static void
write_text (HDC dc, struct tekchar tc)
{
  if (!txt) {
    txt_y = out_y;
    txt_x = out_x;
  }
  if (tc.c < ' ') {
    flush_text(dc);
    short pw = tekfonts[tc.font].wid;
    switch(tc.c) {
      when '\b':  /* BS: left */
        out_x -= pw;
        if (out_x < margin) {
          write_up();
          out_x = 4096 - pw;
        }
      when '\t':  /* HT: right */
        out_x += pw;
        if (out_x + pw > 4096) {
          write_cr();
          write_lf();
        }
      when '\v':  /* VT: up */
        write_up();
      when '\n':  /* LF: down */
        write_lf();
      when '\r':  /* CR: carriage return */
        write_cr();
    }
  }
  else {
    short pw = tc.w * tekfonts[tc.font].wid;
    out_x += pw;
    if (out_x + pw > 4096) {
      flush_text(dc);
      write_cr();
      write_lf();
    }

    txt = renewn(txt, (txt ? wcslen(txt) : 0) + 2);
    txt[txt_len ++] = tc.c;
    txt[txt_len] = 0;
    txt_wid += pw;
  }
  lastfont = tc.font;
}

void
tek_init(int glow)
{
  init_font(0);
  init_font(1);
  init_font(2);
  init_font(3);

  static bool init = false;
  if (!init) {
    tek_reset();
    beam_glow = glow;
    init = true;
  }
}

void
tek_paint(void)
{
  trace_tek();

  /* scale mode, how to map Tek coordinates to window coordinates
     -1 pre-scale
     0 calculate; would need font scaling
     1 post-scale; would need proper calculation of pen width
  */
  short scale_mode = -1;

  // retrieve terminal pixel size (without padding)
  int height, width;
  win_get_pixels(&height, &width, false);

  HDC dc = GetDC(wnd);
  HDC hdc = CreateCompatibleDC(dc);
  HBITMAP hbm = scale_mode == 1
                ? CreateCompatibleBitmap(dc, 4096, 4096)
                : CreateCompatibleBitmap(dc, width, height);
  (void)SelectObject(hdc, hbm);

  // retrieve colour configuration
  colour fg0 = win_get_colour(TEK_FG_COLOUR_I);
  if (fg0 == (colour)-1)
    fg0 = win_get_colour(FG_COLOUR_I);
  colour bg = win_get_colour(TEK_BG_COLOUR_I);
  if (bg == (colour)-1)
    bg = win_get_colour(BG_COLOUR_I);
  colour cc = win_get_colour(TEK_CURSOR_COLOUR_I);
  if (cc == (colour)-1)
    cc = win_get_colour(CURSOR_COLOUR_I);
  // optionally use current text colour?
  //cattr attr = apply_attr_colour(term.curs.attr, ACM_SIMPLE); .truefg/bg

  // adjust colours
  // use full colour for glow or bold (defocused)
  colour glowfg = fg0;
  // derived dimmed default colour
  fg0 = ((fg0 & 0xFEFEFEFE) >> 1) + ((fg0 & 0xFCFCFCFC) >> 2)
                                  + ((bg & 0xFCFCFCFC) >> 2);
  // also dim cursor colour
  cc = ((cc & 0xFEFEFEFE) >> 1) + ((cc & 0xFCFCFCFC) >> 2)
                                + ((bg & 0xFCFCFCFC) >> 2);

  int tx(int x) {
    if (scale_mode)
      return x;
    else
      return x * width / 4096;
  }
  int ty(int y) {
    if (scale_mode)
      return 3120 - y;
    else
      return (3120 - y) * height / 4096;
  }

  XFORM oldxf;
  if (scale_mode == -1 && SetGraphicsMode(hdc, GM_ADVANCED)) {
    GetWorldTransform(hdc, &oldxf);
    XFORM xform = (XFORM){(float)width / 4096.0, 0.0, 0.0, 
                          (float)height / 3120.0, 0.0, 0.0};
    if (!ModifyWorldTransform(hdc, &xform, MWT_LEFTMULTIPLY))
      scale_mode = 0;
  }
  else
    scale_mode = 0;

  txt = 0;
  out_x = 0;
  out_y = 3120 - tekfonts[font].hei;
  margin = 0;
  lastfont = 4;
  for (int i = 0; i < tek_buf_len; i++) {
    struct tekchar * tc = &tek_buf[i];

    // beam glow effect (bright drawing spot)
    if (tc->recent) {
      fg = glowfg;
      tc->recent --;
    }
    else
      fg = fg0;

    int pen_width = scale_mode == 1 ? width / 204 + height / 156 : 0;

    // defocused mode
    if (tc->defocused) {
      // simulate defocused by brighter display
      //fg = glowfg;
      // or by wider pen
      pen_width = width / 204 + height / 156;
      // or by shaded pen; not implemented
    }

    if (tc->type) {
      flush_text(hdc);
      out_x = tc->x;
      out_y = tc->y;
    }
    else {
      if (tc->font != lastfont)
        flush_text(hdc);
      write_text(hdc, tek_buf[i]);
    }

    if (tc->type == TEKMODE_GRAPH0)
      MoveToEx(hdc, tx(tc->x), ty(tc->y), null);
    else if (tc->type == TEKMODE_GRAPH) {
      HPEN pen;
      HPEN create_pen(DWORD style)
      {
#ifdef use_extpen
        LOGBRUSH brush = (LOGBRUSH){BS_HOLLOW, fg, 0};
        return ExtCreatePen(PS_GEOMETRIC | style, pen_width, &brush, 0, 0);
#else
        return CreatePen(style, pen_width, fg);
#endif
      }
      switch (tc->style) {
        // 1 dotted
        when 1: pen = create_pen(PS_DOT);
        // 2 dot-dashed
        when 2: pen = create_pen(PS_DASHDOT);
        // 3 short dashed
        when 3: pen = create_pen(PS_DASHDOTDOT);
        // 4 long dashed
        when 4: pen = create_pen(PS_DASH);
        // 0 solid
        otherwise:
          if (pen_width)
            pen = create_pen(PS_SOLID);
          else
            pen = CreatePen(PS_SOLID, pen_width, fg);
      }
      HPEN oldpen = SelectObject(hdc, pen);
      SetBkMode(hdc, TRANSPARENT);  // stabilize broken vector styles
      LineTo(hdc, tx(tc->x), ty(tc->y));
      oldpen = SelectObject(hdc, oldpen);
      DeleteObject(oldpen);
      // add final point
      SetPixel(hdc, tx(tc->x), ty(tc->y), fg);
    }
    else if (tc->type == TEKMODE_POINT_PLOT)
      SetPixel(hdc, tx(tc->x), ty(tc->y), fg);
  }
  // cursor ▐ or fill rectangle; ❚ spiddly; █▒▓ do not work unclipped
  if (lastfont < 4) {
    if (cc != fg)
      flush_text(hdc);
    fg = cc;
    write_text(hdc, (struct tekchar)
                    {.type = 0, .c = 0x2590, .w = 1, .font = lastfont});
  }
  flush_text(hdc);

  if (scale_mode == -1)
    SetWorldTransform(hdc, &oldxf);

  if (scale_mode == 1)
    StretchBlt(dc,
               PADDING, OFFSET + PADDING,
               width, height,
               hdc,
               0, 0, 4096, 3120, SRCCOPY);
  else
    BitBlt(dc,
           PADDING, OFFSET + PADDING,
           width, height,
           hdc, 0, 0, SRCCOPY);

  DeleteObject(hbm);
  DeleteDC(hdc);
  ReleaseDC(wnd, dc);
}
