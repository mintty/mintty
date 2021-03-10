#include "tek.h"

enum tekmode tek_mode = TEKMODE_OFF;
static enum tekmode tek_mode_pre_gin;
bool tek_bypass = false;
static uchar intensity = 0x7F; // for point modes
static uchar style = 0;        // for vector modes
static uchar font = 0;
static short margin = 0;
static bool beam_defocused = false;
static bool beam_writethru = false;
static bool plotpen = false;
static bool apl_mode = false;

static short tek_y, tek_x;
static short gin_y, gin_x = -1;
static uchar lastfont = 0;
static int lastwidth = -1;
static wchar * tek_dyn_font = 0;

static int beam_glow = 1;
static int thru_glow = 5;

static bool flash = false;

static wchar * copyfn = 0;

static wchar * APL = W(" ¨)<≤=>]∨∧≠÷,+./0123456789([;×:\\¯⍺⊥∩⌊∊_∇∆⍳∘'⎕∣⊤○⋆?⍴⌈∼↓∪ω⊃↑⊂←⊢→≥-⋄ABCDEFGHIJKLMNOPQRSTUVWXYZ{⊣}$ ");

struct tekfont {
  void * f;
  short rows, cols;
  short hei, wid;
} tekfonts[] = {        // Tek		VT240		mintty
  {0, 35, 74, 88, 55},  // 35 × 74	35 × 74		35 × 74
  {0, 38, 81, 81, 50},  // 38 × 81	38 × 81		38 × 81
  {0, 58, 121, 53, 32}, // 58 × 121	58 × 128	58 × 128
  {0, 64, 133, 48, 30}  // 64 × 133	64 × 133	64 × 136 (see out_lf)
};

struct tekchar {
  char type;
  uchar recent;
  bool defocused;
  bool writethru;
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
  uchar font;
  short y, x;
  short intensity;
  uchar style;
#endif
};
static struct tekchar * tek_buf = 0;
static int tek_buf_len = 0;
static int tek_buf_size = 0;

static void
tek_buf_append(struct tekchar * tc)
{
  if (tek_buf_len == tek_buf_size) {
    int new_size = tek_buf_size + 1000;
    struct tekchar * new_buf = renewn(tek_buf, new_size);
    if (!new_buf)
      return;
    tek_buf = new_buf;
    tek_buf_size = new_size;
  }
  tek_buf[tek_buf_len ++] = * tc;
}

static void
tek_buf_clear(void)
{
  if (tek_buf)
    free(tek_buf);
  tek_buf = 0;
  tek_buf_len = 0;
  tek_buf_size = 0;
}


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
  tek_buf_clear();
  flash = true;
  tek_paint();
  flash = false;
  usleep(30000);
  tek_home();
}

void
tek_gin(void)
{
  tek_mode_pre_gin = tek_mode;
  tek_mode = TEKMODE_GIN;
  //gin_y = tek_y;
  //gin_x = tek_x;
  tek_move_by(0, 0);
}

/* PAGE
   Erases the display, resets to Alpha Mode and home position;
   resets to Margin 1 and cancels Bypass condition.
*/
void
tek_page(void)
{
  tek_clear();
  tek_mode = TEKMODE_ALPHA;
  margin = 0;
  tek_bypass = false;
}

/* RESET (xterm)
   Unlike the similarly-named Tektronix “RESET” button, this
   does everything that PAGE does as well as resetting the
   line-type and font-size to their default values.
*/
void
tek_reset(void)
{
  // line type
  style = 0;
  beam_defocused = false;
  beam_writethru = false;
  // font
  font = 0;
  apl_mode = false;
  // clear etc
  tek_page();
  // let's also do this
  intensity = 0x7F;
  // let's better reset everything
  plotpen = false;
  lastfont = 0;
  lastwidth = -1;
  beam_glow = 1;
  thru_glow = 5;
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

  tek_buf_append(&(struct tekchar)
                  {.type = 0,
                   .recent = beam_writethru ? thru_glow : beam_glow,
                   .defocused = beam_defocused, .writethru = beam_writethru,
                   .c = c, .w = width, .font = font});
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
tek_copy(wchar * fn)
{
  if (!copyfn)
    copyfn = fn;
}


#define dont_debug_graph

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
#ifdef debug_graph
  printf("!tek_beam %d defocused %d write-thru %d\n", vector_style, defocused, write_through);
#endif
  beam_defocused = defocused;
  beam_writethru = write_through;
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

void
tek_address(char * code)
{
#ifdef debug_graph
  printf("!tek_address %d <%s>", tek_mode, code);
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

	10 Bits border coordinates
	0100000	1100000	0100000	1000000	y 0 x 0
	0x20	0x60	0x20	0x40	" ` @"
	0100000	1100000	0111111	1011111	y 0 x 1023 (0 3FF)
	0x20	0x60	0x3F	0x5F	" `?_"
	0111000	1101011	0100000	1000000	y 779 x 0 (30B 0)
	0x38	0x6B	0x20	0x40	"8k @"
	0111000	1101011	0111111	1011111	y 779 x 1023 (30B 3FF)
	0x38	0x6B	0x3F	0x5F	"8k?_"
  */
  // accumulate tags for switching; clear tags from input
  short tag = 0;
  char * tc = code;
  while (* tc) {
    tag = (tag << 2) | ((* tc >> 5) & 3);
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
  tek_buf_append(&(struct tekchar)
                  {.type = tek_mode,
                   .recent = beam_writethru ? thru_glow : beam_glow,
                   .defocused = beam_defocused, .writethru = beam_writethru,
                   .y = tek_y, .x = tek_x,
                   .style = style, .intensity = intensity});
  // 3-10, ALPHA MODE 22.
  margin = 0;
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
    tek_buf_append(&(struct tekchar)
                    {.type = TEKMODE_POINT_PLOT,
                     .recent = beam_writethru ? thru_glow : beam_glow,
                     .defocused = beam_defocused, .writethru = beam_writethru,
                     .y = tek_y, .x = tek_x, .intensity = intensity});
  }
  else {
    tek_buf_append(&(struct tekchar)
                    {.type = TEKMODE_GRAPH0,
                     .recent = beam_writethru ? thru_glow : beam_glow,
                     .defocused = beam_defocused, .writethru = beam_writethru,
                     .y = tek_y, .x = tek_x, .intensity = 0});
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
    else if (tc->type == TEKMODE_POINT_PLOT || tc->type == TEKMODE_SPECIAL_PLOT)
      printf("plot %4d %4d\n", tc->y, tc->x);
    else
      printf("text %04X:%d\n", tc->c, tc->w);
  }

  ptd = true;
#endif
}


static void
fix_gin()
{
  if (gin_y < 0)
    gin_y = 0;
  if (gin_y > 3119)
    gin_y = 3119;
  if (gin_x < 0)
    gin_x = 0;
  if (gin_x > 4095)
    gin_x = 4095;
}

void
tek_move_by(int dy, int dx)
{
  //printf("tek_move_by %d:%d\n", dy, dx);
  if (dy || dx) {
    gin_y += dy;
    gin_x += dx;
    fix_gin();
  }
  else if (gin_x < 0) {
    gin_y = 1560;
    gin_x = 2048;
  }
  tek_paint();  // Smooth GIN mode crosshair cursor movement.
}

#include <windows.h>
#include "winpriv.h"
#include "child.h"

void
tek_move_to(int y, int x)
{
  //printf("tek_move_to %d:%d\n", y, x);
  int height, width;
  win_get_pixels(&height, &width, false);

  int pad_l = 0, pad_t = 0;
  if (width > height * 4096 / 3120) {
    // width factor > height factor; reduce width
    int w = height * 4096 / 3120;
    pad_l = (width - w) / 2;
    width = w;
  }
  else if (height > width * 3120 / 4096) {
    // height factor > width factor; reduce height
    int h = width * 3120 / 4096;
    pad_t = (height - h) / 2;
    height = h;
  }

  gin_y = 3119 - (y - pad_t) * 3120 / height;
  gin_x = (x - pad_l) * 4096 / width;
  fix_gin();
  tek_paint();  // Smooth GIN mode crosshair cursor movement.
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

void
tek_set_font(wchar * fn)
{
  if (tek_dyn_font)
    free(tek_dyn_font);
  tek_dyn_font = fn;
}

static void
init_font(short f)
{
  if (tekfonts[f].f)
    DeleteObject(tekfonts[f].f);

  wstring fn = tek_dyn_font ?: *cfg.tek_font ? cfg.tek_font : cfg.font.name;
  tekfonts[f].f = CreateFontW(
                  - tekfonts[f].hei, - tekfonts[f].wid, 
                  0, 0, FW_NORMAL, 0, 0, 0,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  get_font_quality(), FIXED_PITCH | FF_DONTCARE,
                  fn);
}

static colour fg;
static short txt_y, txt_x;
static short out_y, out_x;
static wchar * txt = 0;
static int txt_len = 0;
static int txt_wid = 0;

static void
tek_send_address_0(int strap)
{
  short y, x;
  if (tek_mode == TEKMODE_GIN) {
    y = gin_y;
    x = gin_x;
  }
  else {
    y = out_y;
    x = out_x;
  }

  child_printf("%c%c%c%c",
               0x20 | (x >> 7), 0x60 | ((x >> 2) & 0x1F),
               0x20 | (y >> 7), 0x40 | ((y >> 2) & 0x1F));
  if (strap) {
    if (strap > 1)
      child_write("\r\003", 2);
    else
      child_write("\r", 1);
  }
}

void
tek_send_address(void)
{
  tek_send_address_0(cfg.tek_strap);
  // 3-18, GIN MODE 45.: return to Alpha mode
  tek_mode = TEKMODE_ALPHA;
  // or rather restore previous mode for smooth interactive drawing?
  tek_mode = tek_mode_pre_gin;
  // 3-10, ALPHA MODE 22.
  margin = 0;
}

void
tek_enq(void)
{
  if (tek_mode == TEKMODE_GIN) {
    // 3-17, GIN MODE 44.: ENQ while in GIN mode
    tek_send_address_0(false);
    return;
  }

  char status = 0x30;
  if (cfg.tek_strap)
    status |= 0x80;
  if (tek_mode == TEKMODE_ALPHA)
    status |= 0x04;
  else
    status |= 0x08;
  if (margin)
    status |= 0x02;
  child_write(&status, 1);
  tek_send_address_0(cfg.tek_strap);
  // 3-17, GIN MODE 41., 42.: stay in current mode
}

static void
out_text(HDC dc, short x, short y, wchar * s, uchar f)
{
  SelectObject(dc, tekfonts[f].f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);

  int len = wcslen(s);
  int dxs[len];
  for (int i = 0; i < len; i++)
    dxs[i] = tekfonts[f].wid * lastwidth;
  ExtTextOutW(dc, x, y, 0, 0, s, len, dxs);
}

static void
out_flush(HDC dc)
{
  if (txt) {
    if (!lastwidth) {
      // fix position of combining character
      short pw = tekfonts[lastfont].wid;
      txt_x -= pw;
    }
    //printf("%d <%ls> [%d]\n", txt_len, txt, lastwidth);
    out_text(dc, txt_x, 3120 - txt_y - tekfonts[font].hei, txt, lastfont & 3);
    free(txt);
    txt = 0;
    txt_len = 0;
    txt_wid = 0;
  }
}

static void
out_cr(void)
{
  out_x = margin;
}

static void
out_lf(void)
{
  short ph = tekfonts[lastfont & 3].hei;
  out_y -= ph;
  // "<=" rather than "<" to skip last pixel line,
  // to adjust smallest character size mode to original 64 lines
  if (out_y <= 0) {
    out_y = 3120 - ph;
    margin = 2048 - margin;
    out_x = (out_x + 2048) % 4096;
  }
}

static void
out_up(void)
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
out_char(HDC dc, struct tekchar * tc)
{
  if (tc->c < ' ') {
    out_flush(dc);
    short pw = tekfonts[tc->font].wid;
    switch(tc->c) {
      when '\b':  /* BS: left */
        out_x -= pw;
        if (out_x < margin) {
          out_up();
          out_x = 4096 - pw;
        }
      when '\t':  /* HT: right */
        out_flush(dc);
        if (out_x + pw > 4096) {
          out_cr();
          out_lf();
        }
        out_x += pw;
      when '\v':  /* VT: up */
        out_up();
      when '\n':  /* LF: down */
        out_lf();
      when '\r':  /* CR: carriage return */
        out_cr();
    }
  }
  else {
    if (tc->w != lastwidth || !tc->w) {
      out_flush(dc);
    }
    lastwidth = tc->w;
    short pw = tc->w * tekfonts[tc->font].wid;
    //printf("out %02X @%d:%d\n", tc->c, out_y, out_x);

    // line wrap-around
    if (out_x + pw > 4096) {
      out_flush(dc);
      out_cr();
      out_lf();
      //printf("wrapped -> @%d:%d\n", out_y, out_x);
    }

    if (!txt) {
      txt_y = out_y;
      txt_x = out_x;
      //printf("txt %02X @%d:%d\n", tc->c, out_y, out_x);
    }

    txt = renewn(txt, (txt ? wcslen(txt) : 0) + 2);
    txt[txt_len ++] = tc->c;
    txt[txt_len] = 0;
    txt_wid += pw;

    out_x += pw;
  }
  lastfont = tc->font;
}

void
tek_init(bool reset, int glow)
{
  init_font(0);
  init_font(1);
  init_font(2);
  init_font(3);

  if (reset)
    tek_reset();

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
  //unsigned long now = mtime();
  trace_tek();

  /* scale mode, how to map Tek coordinates to window coordinates
     -1 pre-scale
     0 calculate; would need font scaling
     1 post-scale; would need proper calculation of pen width
  */
  short scale_mode = -1;

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

  // retrieve terminal pixel size (without padding)
  int height, width;
  win_get_pixels(&height, &width, false);
  if (copyfn) {
    height = 780;
    width = 1024;
    // check if any 12-bit graphics addressing is used
    for (int i = 0; i < tek_buf_len; i++) {
      struct tekchar * tc = &tek_buf[i];
      if (tc->type && ((tc->x & 3) || (tc->y & 3))) {
        // select high resolution to reflect 12 bit addressing
        height = 3120;
        width = 4096;
        break;
      }
    }
  }

  // align to aspect ratio
  int pad_l = 0, pad_t = 0;
  int pad_r = 0, pad_b = 0;
  if (width > height * 4096 / 3120) {
    // width factor > height factor; reduce width
    int w = height * 4096 / 3120;
    pad_l = (width - w) / 2;
    pad_r = width - w - pad_l;
    width = w;
  }
  else if (height > width * 3120 / 4096) {
    // height factor > width factor; reduce height
    int h = width * 3120 / 4096;
    pad_t = (height - h) / 2;
    pad_b = height - h - pad_t;
    height = h;
  }
  (void)pad_r; (void)pad_b;  // could be used to clear outer pane

  HDC dc = GetDC(wnd);
  HDC hdc = CreateCompatibleDC(dc);
  HBITMAP hbm = scale_mode == 1
                ? CreateCompatibleBitmap(dc, 4096, 3120)
                : CreateCompatibleBitmap(dc, width, height);
  (void)SelectObject(hdc, hbm);

  // fill background
  if (flash)
    bg = fg0;
  HBRUSH bgbr = CreateSolidBrush(bg);
  if (scale_mode == 1)
    FillRect(hdc, &(RECT){0, 0, 4096, 3120}, bgbr);
  else
    FillRect(hdc, &(RECT){0, 0, width, height}, bgbr);
  DeleteObject(bgbr);

  int tx(int x) {
    x -= 1;  // heuristic adjustment to compensate for coordinate rounding
    if (scale_mode)
      return x;
    else
      return x * width / 4096;
  }
  int ty(int y) {
    y += 2;  // heuristic adjustment to compensate for coordinate rounding
    if (scale_mode)
      return 3119 - y;
    else
      return (3119 - y) * height / 4096;
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

  int pen_width0 = scale_mode == 1
                   ? width / 204 + height / 156
                   : (width + height) / 1600;
  // in scale_modes 0 or -1, for full width (3120×4096) pen_width should be 4
  //printf("pen width %d\n", pen_width0);

  txt = 0;
  out_x = 0;
  out_y = 3120 - tekfonts[font].hei;
  margin = 0;
  lastfont = 4;
  //printf("tek_paint %d %p\n", tek_buf_len, tek_buf);
  //static int trc = -1;
  for (int i = 0; i < tek_buf_len; i++) {
    struct tekchar * tc = &tek_buf[i];

    int pen_width = pen_width0;
    fg = fg0;

    // defocused mode
    if (tc->defocused) {
      // simulate defocused by brighter display
      //fg = glowfg;
      if (cfg.tek_defocused_colour != (colour)-1)
        fg = cfg.tek_defocused_colour;
      else {  // bolden colour
        int r = red(fg);
        int g = green(fg);
        int b = blue(fg);
        int _r = red(bg);
        int _g = green(bg);
        int _b = blue(bg);
        r = (r - _r) * 150 / 100 + _r;
        g = (g - _g) * 150 / 100 + _g;
        b = (b - _b) * 150 / 100 + _b;
        r = min(255, max(0, r));
        g = min(255, max(0, g));
        b = min(255, max(0, b));
        fg = RGB(r, g, b);
      }

      // display defocused by wider pen
      pen_width = (pen_width ?: 1) * 12;
      //printf("defocused pen width %d\n", pen_width);
      // or by shaded pen; not implemented
    }

    // write-thru mode and beam glow effect (bright drawing spot)
    if (tc->writethru) {
      if (tc->recent) {
        // simulate Write-Thru by distinct colour?
        //fg = RGB(200, 100, 0);
        // fade out?
        if (tc->recent <= (thru_glow + 1) / 2) {
          //printf("fade %06X", fg);
          fg = ((fg & 0xFEFEFEFE) >> 1) + ((bg & 0xFEFEFEFE) >> 1);
          //printf(" -> %06X\n", fg);
        }

        tc->recent--;
      }
      else {
        // simulate faded Write-Thru by distinct colour?
        //fg = RGB(200, 100, 0);
        if (cfg.tek_write_thru_colour != (colour)-1)
          fg = cfg.tek_write_thru_colour;
        else {
          //printf("fade %06X", fg);
          fg = ((fg & 0xFEFEFEFE) >> 1) + ((bg & 0xFEFEFEFE) >> 1);
          //printf(" -> %06X", fg);
          fg = RGB(green(fg), blue(fg), red(fg));
          //printf(" -> %06X\n", fg);
        }
        // fade away?
        //fg = bg;
      }
    }
    else if (tc->recent) {
      fg = glowfg;
      tc->recent --;
    }

    if (tc->type) {
      out_flush(hdc);
      out_x = tc->x;
      out_y = tc->y;
    }
    else {
      if (tc->font != lastfont)
        out_flush(hdc);
      out_char(hdc, &tek_buf[i]);
      // update graphic cursor in case of subsequent written first vector
      MoveToEx(hdc, tx(out_x), ty(out_y), null);
    }

    if (tc->type == TEKMODE_GRAPH0) {
      //printf("MoveTo (%d:%d) %d:%d\n", tc->y, tc->x, ty(tc->y), tx(tc->x));
      MoveToEx(hdc, tx(tc->x), ty(tc->y), null);
    }
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
      //printf("style %d defoc %d pw %d\n", tc->style, tc->defocused, pen_width);
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
      //printf("LineTo (%d:%d) %d:%d\n", tc->y, tc->x, ty(tc->y), tx(tc->x));
      LineTo(hdc, tx(tc->x), ty(tc->y));
      oldpen = SelectObject(hdc, oldpen);
      DeleteObject(oldpen);
      // add final point
      //SetPixel(hdc, tx(tc->x), ty(tc->y), fg);
      pen = CreatePen(PS_SOLID, pen_width / 4, fg);
      oldpen = SelectObject(hdc, pen);
      int delta = pen_width / 4;
      Ellipse(hdc, tx(tc->x - delta), ty(tc->y - delta), tx(tc->x + delta), ty(tc->y + delta));
      oldpen = SelectObject(hdc, oldpen);
      DeleteObject(oldpen);
    }
    else if (tc->type == TEKMODE_POINT_PLOT || tc->type == TEKMODE_SPECIAL_PLOT) {
      if (tc->intensity == 0x7F && !tc->defocused)
        SetPixel(hdc, tx(tc->x), ty(tc->y), fg);
      else {
        static short intensify[64] =
          { 0,  1,  1,  1,   1,  1,  1,  2,    2,  2,  2,  2,   3,  3,  3,  3,
            4,  4,  4,  5,   5,  5,  6,  6,    7,  8,  9, 10,  11, 12, 12, 13,
           14, 16, 17, 19,  20, 22, 23, 25,   28, 31, 34, 38,  41, 44, 47, 50,
           56, 62, 69, 75,  81, 88, 94, 100,  56, 63, 69, 75,  81, 88, 96, 100
          };
        int r = red(fg);
        int g = green(fg);
        int b = blue(fg);
        int _r = red(bg);
        int _g = green(bg);
        int _b = blue(bg);
#ifdef linear_intensify
        short i = tc->intensity & 0x3F;
        if (i > 55)
          i -= 8;
        r = (r - _r) * i / 55 + _r;
        g = (g - _g) * i / 55 + _g;
        b = (b - _b) * i / 55 + _b;
#else
        r = (r - _r) * intensify[tc->intensity & 0x3F] / 100 + _r;
        g = (g - _g) * intensify[tc->intensity & 0x3F] / 100 + _g;
        b = (b - _b) * intensify[tc->intensity & 0x3F] / 100 + _b;
#endif
        colour fgpix = RGB(r, g, b);
        //trc *= trc;
        //if (trc) printf("fg %06X bg %06X (wt %d df %d) int [%2d]*%3d -> %06X (w %d)\n", fg, bg, tc->writethru, tc->defocused, tc->intensity, intensify[tc->intensity & 0x3F], fgpix, pen_width);
        if (tc->defocused) {
          HPEN pen = CreatePen(PS_SOLID, pen_width, fgpix);
          HPEN oldpen = SelectObject(hdc, pen);
          int delta = 4;
          Ellipse(hdc, tx(tc->x - delta), ty(tc->y - delta), tx(tc->x + delta), ty(tc->y + delta));
          oldpen = SelectObject(hdc, oldpen);
          DeleteObject(oldpen);
        }
        else
          SetPixel(hdc, tx(tc->x), ty(tc->y), fgpix);
      }
    }
  }
  //if (trc == 1) trc = false;

  // text cursor
  if ((tek_mode == TEKMODE_ALPHA ||
       (tek_mode == TEKMODE_GIN && tek_mode_pre_gin == TEKMODE_ALPHA)
      ) && !copyfn && lastfont < 4 && term.cblinker
     )
  {
    if (cc != fg)
      out_flush(hdc);
    fg = cc;
    out_char(hdc, &(struct tekchar)
                   {.type = 0, .c = 0x2588,  // ▐ half ❚ spiddly █▒▓
                    .w = 1, .font = lastfont});
  }
  out_flush(hdc);

  // GIN mode crosshair cursor
  if (tek_mode == TEKMODE_GIN) {
    fg = ((fg0 & 0xFEFEFEFE) >> 1) + ((bg & 0xFEFEFEFE) >> 1);
    HPEN pen = CreatePen(PS_SOLID, pen_width0, fg);
    HPEN oldpen = SelectObject(hdc, pen);
    SetBkMode(hdc, TRANSPARENT);  // stabilize broken vector styles

    //printf("GIN %d:%d\n", gin_y, gin_x);
    MoveToEx(hdc, tx(0), ty(gin_y), null);
    LineTo(hdc, tx(4096), ty(gin_y));
    MoveToEx(hdc, tx(gin_x), ty(0), null);
    LineTo(hdc, tx(gin_x), ty(3120));

    oldpen = SelectObject(hdc, oldpen);
    DeleteObject(oldpen);
  }

  if (scale_mode == -1)
    SetWorldTransform(hdc, &oldxf);

  if (copyfn) {
    save_img(hdc, 0, 0, width, height, copyfn);
    free(copyfn);
    copyfn = 0;
  }
  else {
    if (scale_mode == 1)
      StretchBlt(dc,
                 PADDING + pad_l, OFFSET + PADDING + pad_t,
                 width, height,
                 hdc,
                 0, 0, 4096, 3120, SRCCOPY);
    else
      BitBlt(dc,
             PADDING + pad_l, OFFSET + PADDING + pad_t,
             width, height,
             hdc, 0, 0, SRCCOPY);
  }

  DeleteObject(hbm);
  DeleteDC(hdc);
  ReleaseDC(wnd, dc);
  //printf("tek_painted %ld\n", mtime() - now);
}
