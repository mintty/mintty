#ifndef SIXEL_H
#define SIXEL_H

#include "config.h"

#define DECSIXEL_PARAMS_MAX 16
#define DECSIXEL_PALETTE_MAX 1024
#define DECSIXEL_PARAMVALUE_MAX 65535
#define DECSIXEL_WIDTH_MAX 4096
#define DECSIXEL_HEIGHT_MAX 4096

typedef unsigned short sixel_color_no_t;

typedef struct sixel_image_buffer {
  sixel_color_no_t * data;
  int width;
  int height;
  colour palette[DECSIXEL_PALETTE_MAX];
  sixel_color_no_t ncolors;
  int palette_modified;
  int use_private_register;
} sixel_image_t;

typedef enum parse_state {
  PS_ESC        = 1,  /* ESC */
  PS_DECSIXEL   = 2,  /* DECSIXEL body part ", $, -, ? ... ~ */
  PS_DECGRA     = 3,  /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
  PS_DECGRI     = 4,  /* DECGRI Graphics Repeat Introducer ! Pn Ch */
  PS_DECGCI     = 5,  /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
} parse_state_t;

typedef struct parser_context {
  parse_state_t state;
  int pos_x;
  int pos_y;
  int max_x;
  int max_y;
  int attributed_pan;
  int attributed_pad;
  int attributed_ph;
  int attributed_pv;
  int repeat_count;
  int color_index;
  int bgindex;
  int grid_width;
  int grid_height;
  int param;
  int nparams;
  int params[DECSIXEL_PARAMS_MAX];
  sixel_image_t image;
} sixel_state_t;

int sixel_parser_init(sixel_state_t * st, colour fgcolor, colour bgcolor, int use_private_register);
int sixel_parser_parse(sixel_state_t * st, unsigned char * p, int len);
int sixel_parser_set_default_color(sixel_state_t * st);
unsigned char * sixel_parser_finalize(sixel_state_t * st);
void sixel_parser_deinit(sixel_state_t * st);

#endif
