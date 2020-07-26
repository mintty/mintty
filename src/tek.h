#include <wchar.h>

// https://vt100.net/docs/vt3xx-gp/chapter13.html#F13-2
enum tekmode {
  TEKMODE_OFF, TEKMODE_ALPHA, 
  TEKMODE_GRAPH0, TEKMODE_GRAPH, TEKMODE_POINT_PLOT, TEKMODE_SPECIAL_PLOT, 
  TEKMODE_INCREMENTAL_PLOT, 
  TEKMODE_GIN
};

extern enum tekmode tek_mode;
extern bool tek_bypass;

extern void tek_page(void);
extern void tek_reset(void);
extern void tek_init(bool reset, int glow);
extern void tek_gin(void);

extern void tek_font(short f);
extern void tek_write(wchar c, int width);
extern void tek_enq(void);
extern void tek_alt(bool);
extern void tek_copy(wchar * fn);
extern void tek_clear(void);
extern void tek_set_font(wchar * fn);

extern void tek_move_to(int y, int x);
extern void tek_move_by(int dy, int dx);
extern void tek_send_address(void);

extern void tek_beam(bool defocused, bool write_through, char vector_style);
extern void tek_intensity(bool defocused, int intensity);

extern void tek_address(char *);
extern void tek_pen(bool on);
extern void tek_step(char c);

extern void tek_paint(void);

// avoid #include "winimg.h"
extern void save_img(HDC, int x, int y, int w, int h, wstring fn);

