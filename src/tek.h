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

extern void tek_reset(void);
extern void tek_init(int glow);

extern void tek_font(short f);
extern void tek_write(wchar c, int width);
extern void tek_send_address(void);
extern void tek_enq(void);
extern void tek_alt(bool);
extern void tek_copy(void);
extern void tek_clear(void);

extern void tek_beam(bool defocused, bool write_through, char vector_style);
extern void tek_intensity(bool defocused, int intensity);

extern void tek_address(char *);
extern void tek_pen(bool on);
extern void tek_step(char c);

extern void tek_paint(void);
