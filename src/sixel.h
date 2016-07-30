#ifndef SIXEL_H
#define SIXEL_H

#include "config.h"

#define DECSIXEL_PARAMS_MAX 16

typedef struct sixel_image_buffer {
    unsigned char *data;
    int width;
    int height;
    int palette[256];
    int ncolors;
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
    int param;
    int nparams;
    int params[DECSIXEL_PARAMS_MAX];
    sixel_image_t image;
} sixel_state_t;

int sixel_parser_init(sixel_state_t *context);
int sixel_parser_parse(sixel_state_t *context, unsigned char *p, int len);
int sixel_parser_finalize(sixel_state_t *context);

#endif
