// sixel.c (part of mintty)
// Copyright 2016 Hayaki Saito
// hls_to_rgb() function is derived from graphics.c in Xterm pl#310 originally written by Ross Combs.
// Licensed under the terms of the GNU General Public License v3 or later.

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>   /* isdigit */
#include <string.h>  /* memcpy */

#include "sixel.h"

#define SIXEL_RGB(r, g, b) (((r) << 16) + ((g) << 8) +  (b))
#define PALVAL(n,a,m) (((n) * (a) + ((m) / 2)) / (m))
#define SIXEL_XRGB(r,g,b) SIXEL_RGB(PALVAL(r, 255, 100), PALVAL(g, 255, 100), PALVAL(b, 255, 100))

static int const sixel_default_color_table[] = {
    SIXEL_XRGB(0,  0,  0),   /*  0 Black    */
    SIXEL_XRGB(20, 20, 20),  /*  1 Blue     */
    SIXEL_XRGB(80, 13, 13),  /*  2 Red      */
    SIXEL_XRGB(20, 80, 20),  /*  3 Green    */
    SIXEL_XRGB(80, 20, 80),  /*  4 Magenta  */
    SIXEL_XRGB(20, 80, 80),  /*  5 Cyan     */
    SIXEL_XRGB(80, 80, 20),  /*  6 Yellow   */
    SIXEL_XRGB(53, 53, 53),  /*  7 Gray 50% */
    SIXEL_XRGB(26, 26, 26),  /*  8 Gray 25% */
    SIXEL_XRGB(33, 33, 60),  /*  9 Blue*    */
    SIXEL_XRGB(60, 26, 26),  /* 10 Red*     */
    SIXEL_XRGB(33, 60, 33),  /* 11 Green*   */
    SIXEL_XRGB(60, 33, 60),  /* 12 Magenta* */
    SIXEL_XRGB(33, 60, 60),  /* 13 Cyan*    */
    SIXEL_XRGB(60, 60, 33),  /* 14 Yellow*  */
    SIXEL_XRGB(80, 80, 80),  /* 15 Gray 75% */
};

/*
 * Primary color hues:
 *  blue:  0 degrees
 *  red:   120 degrees
 *  green: 240 degrees
 */
static int
hls_to_rgb(int hue, int lum, int sat)
{
  double hs = (hue + 240) % 360;
  double hv = hs / 360.0;
  double lv = lum / 100.0;
  double sv = sat / 100.0;
  double c, x, m, c2;
  double r1, g1, b1;
  int r, g, b;
  int hpi;

  if (sat == 0) {
    r = g = b = lum * 255 / 100;
    return SIXEL_RGB(r, g, b);
  }

  if ((c2 = ((2.0 * lv) - 1.0)) < 0.0) {
    c2 = -c2;
  }
  c = (1.0 - c2) * sv;
  hpi = (int) (hv * 6.0);
  x = (hpi & 1) ? c : 0.0;
  m = lv - 0.5 * c;

  switch (hpi) {
  case 0:
    r1 = c;
    g1 = x;
    b1 = 0.0;
    break;
  case 1:
    r1 = x;
    g1 = c;
    b1 = 0.0;
    break;
  case 2:
    r1 = 0.0;
    g1 = c;
    b1 = x;
    break;
  case 3:
    r1 = 0.0;
    g1 = x;
    b1 = c;
    break;
  case 4:
    r1 = x;
    g1 = 0.0;
    b1 = c;
    break;
  case 5:
    r1 = c;
    g1 = 0.0;
    b1 = x;
    break;
  default:
    return SIXEL_RGB(255, 255, 255);
  }

  r = (int) ((r1 + m) * 100.0 + 0.5);
  g = (int) ((g1 + m) * 100.0 + 0.5);
  b = (int) ((b1 + m) * 100.0 + 0.5);

  if (r < 0) {
    r = 0;
  } else if (r > 100) {
    r = 100;
  }
  if (g < 0) {
    g = 0;
  } else if (g > 100) {
    g = 100;
  }
  if (b < 0) {
    b = 0;
  } else if (b > 100) {
    b = 100;
  }
  return SIXEL_RGB(r * 255 / 100, g * 255 / 100, b * 255 / 100);
}


static int
sixel_image_init(
  sixel_image_t    *image,
  int         width,
  int         height,
  int         bgindex)
{
  int status = (-1);
  size_t size;
  int i;
  int n;
  int r;
  int g;
  int b;

  size = (size_t)(width * height) * sizeof(unsigned char);
  image->width = width;
  image->height = height;
  image->data = (unsigned char *)malloc(size);
  image->ncolors = 2;

  if (image->data == NULL) {
    status = (-1);
    goto end;
  }
  memset(image->data, bgindex, size);

  /* palette initialization */
  for (n = 0; n < 16; n++) {
    image->palette[n] = sixel_default_color_table[n];
  }

  /* colors 16-231 are a 6x6x6 color cube */
  for (r = 0; r < 6; r++) {
    for (g = 0; g < 6; g++) {
      for (b = 0; b < 6; b++) {
        image->palette[n++] = SIXEL_RGB(r * 51, g * 51, b * 51);
      }
    }
  }

  /* colors 232-255 are a grayscale ramp, intentionally leaving out */
  for (i = 0; i < 24; i++) {
    image->palette[n++] = SIXEL_RGB(i * 11, i * 11, i * 11);
  }

  for (; n < 256; n++) {
    image->palette[n] = SIXEL_RGB(255, 255, 255);
  }

  status = (0);

end:
  return status;
}


static int
image_buffer_resize(
  sixel_image_t   *image,
  int         width,
  int         height,
  int         bgindex)
{
  int status = (-1);
  size_t size;
  unsigned char *alt_buffer;
  int n;
  int min_height;

  size = (size_t)(width * height);
  alt_buffer = (unsigned char *)malloc(size);
  if (alt_buffer == NULL) {
    /* free source image */
    free(image->data);
    image->data = NULL;
    status = (-1);
    goto end;
  }

  min_height = height > image->height ? image->height: height;
  if (width > image->width) {  /* if width is extended */
    for (n = 0; n < min_height; ++n) {
      /* copy from source image */
      memcpy(alt_buffer + width * n,
           image->data + image->width * n,
           (size_t)image->width);
      /* fill extended area with background color */
      memset(alt_buffer + width * n + image->width,
           bgindex,
           (size_t)(width - image->width));
    }
  } else {
    for (n = 0; n < min_height; ++n) {
      /* copy from source image */
      memcpy(alt_buffer + width * n,
           image->data + image->width * n,
           (size_t)width);
    }
  }

  if (height > image->height) {  /* if height is extended */
    /* fill extended area with background color */
    memset(alt_buffer + width * image->height,
         bgindex,
         (size_t)(width * (height - image->height)));
  }

  /* free source image */
  free(image->data);

  image->data = alt_buffer;
  image->width = width;
  image->height = height;

  status = (0);

end:
  return status;
}


int
sixel_parser_init(sixel_state_t *st)
{
  int status = (-1);

  st->state = PS_DECSIXEL;
  st->pos_x = 0;
  st->pos_y = 0;
  st->max_x = 0;
  st->max_y = 0;
  st->attributed_pan = 2;
  st->attributed_pad = 1;
  st->attributed_ph = 0;
  st->attributed_pv = 0;
  st->repeat_count = 1;
  st->color_index = 15;
  st->bgindex = (-1);
  st->nparams = 0;
  st->param = 0;

  /* buffer initialization */
  status = sixel_image_init(&st->image, 1, 1, st->bgindex);

  return status;
}

int
sixel_parser_finalize(sixel_state_t *st)
{
  int status = (-1);
  sixel_image_t *image = &st->image;

  if (++st->max_x < st->attributed_ph) {
    st->max_x = st->attributed_ph;
  }

  if (++st->max_y < st->attributed_pv) {
    st->max_y = st->attributed_pv;
  }

  if (image->width > st->max_x || image->height > st->max_y) {
    status = image_buffer_resize(image, st->max_x, st->max_y, st->bgindex);
    if (status < 0) {
      goto end;
    }
  }

  status = (0);

end:
  return status;
}


/* convert sixel data into indexed pixel bytes and palette data */
int sixel_parser_parse(sixel_state_t *st, unsigned char *p, int len)
{
  int status = (-1);
  int n;
  int i;
  int y;
  int bits;
  int sixel_vertical_mask;
  int sx;
  int sy;
  int c;
  int pos;
  unsigned char *p0 = p;
  sixel_image_t *image = &st->image;

  while (p < p0 + len) {
    switch (st->state) {
    case PS_ESC:
      sixel_parser_finalize(st);
      goto end;

    case PS_DECSIXEL:
      switch (*p) {
      case '\x1b':
        st->state = PS_ESC;
        p++;
        break;
      case '"':
        st->param = 0;
        st->nparams = 0;
        st->state = PS_DECGRA;
        p++;
        break;
      case '!':
        st->param = 0;
        st->nparams = 0;
        st->state = PS_DECGRI;
        p++;
        break;
      case '#':
        st->param = 0;
        st->nparams = 0;
        st->state = PS_DECGCI;
        p++;
        break;
      case '$':
        /* DECGCR Graphics Carriage Return */
        st->pos_x = 0;
        p++;
        break;
      case '-':
        /* DECGNL Graphics Next Line */
        st->pos_x = 0;
        st->pos_y += 6;
        p++;
        break;
      default:
        if (*p >= '?' && *p <= '~') {  /* sixel characters */
          if (image->width < (st->pos_x + st->repeat_count) || image->height < (st->pos_y + 6)) {
            sx = image->width * 2;
            sy = image->height * 2;
            while (sx < (st->pos_x + st->repeat_count) || sy < (st->pos_y + 6)) {
              sx *= 2;
              sy *= 2;
            }
            status = image_buffer_resize(image, sx, sy, st->bgindex);
            if (status < 0) {
              goto end;
            }
          }

          if (st->color_index > image->ncolors) {
            image->ncolors = st->color_index;
          }

          bits = *p - '?';

          if (bits == 0) {
            st->pos_x += st->repeat_count;
          } else {
            sixel_vertical_mask = 0x01;
            if (st->repeat_count <= 1) {
              for (i = 0; i < 6; i++) {
                if ((bits & sixel_vertical_mask) != 0) {
                  pos = image->width * (st->pos_y + i) + st->pos_x;
                  image->data[pos] = st->color_index;
                  if (st->max_x < st->pos_x) {
                    st->max_x = st->pos_x;
                  }
                  if (st->max_y < (st->pos_y + i)) {
                    st->max_y = st->pos_y + i;
                  }
                }
                sixel_vertical_mask <<= 1;
              }
              st->pos_x += 1;
            } else {
              /* st->repeat_count > 1 */
              for (i = 0; i < 6; i++) {
                if ((bits & sixel_vertical_mask) != 0) {
                  c = sixel_vertical_mask << 1;
                  for (n = 1; (i + n) < 6; n++) {
                    if ((bits & c) == 0) {
                      break;
                    }
                    c <<= 1;
                  }
                  for (y = st->pos_y + i; y < st->pos_y + i + n; ++y) {
                    memset(image->data + image->width * y + st->pos_x,
                         st->color_index,
                         (size_t)st->repeat_count);
                  }
                  if (st->max_x < (st->pos_x + st->repeat_count - 1)) {
                    st->max_x = st->pos_x + st->repeat_count - 1;
                  }
                  if (st->max_y < (st->pos_y + i + n - 1)) {
                    st->max_y = st->pos_y + i + n - 1;
                  }
                  i += (n - 1);
                  sixel_vertical_mask <<= (n - 1);
                }
                sixel_vertical_mask <<= 1;
              }
              st->pos_x += st->repeat_count;
            }
          }
          st->repeat_count = 1;
        }
        p++;
        break;
      }
      break;

    case PS_DECGRA:
      /* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
      switch (*p) {
      case '\x1b':
        st->state = PS_ESC;
        p++;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        st->param = st->param * 10 + *p - '0';
        p++;
        break;
      case ';':
        if (st->nparams < DECSIXEL_PARAMS_MAX) {
          st->params[st->nparams++] = st->param;
        }
        st->param = 0;
        p++;
        break;
      default:
        if (st->nparams < DECSIXEL_PARAMS_MAX) {
          st->params[st->nparams++] = st->param;
        }
        if (st->nparams > 0) {
          st->attributed_pad = st->params[0];
        }
        if (st->nparams > 1) {
          st->attributed_pan = st->params[1];
        }
        if (st->nparams > 2 && st->params[2] > 0) {
          st->attributed_ph = st->params[2];
        }
        if (st->nparams > 3 && st->params[3] > 0) {
          st->attributed_pv = st->params[3];
        }

        if (st->attributed_pan <= 0) {
          st->attributed_pan = 1;
        }
        if (st->attributed_pad <= 0) {
          st->attributed_pad = 1;
        }

        if (image->width < st->attributed_ph ||
            image->height < st->attributed_pv) {
          sx = st->attributed_ph;
          if (image->width > st->attributed_ph) {
            sx = image->width;
          }

          sy = st->attributed_pv;
          if (image->height > st->attributed_pv) {
            sy = image->height;
          }

          status = image_buffer_resize(image, sx, sy, st->bgindex);
          if (status < 0) {
            goto end;
          }
        }
        st->state = PS_DECSIXEL;
        st->param = 0;
        st->nparams = 0;
      }
      break;

    case PS_DECGRI:
      /* DECGRI Graphics Repeat Introducer ! Pn Ch */
      switch (*p) {
      case '\x1b':
        st->state = PS_ESC;
        p++;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        st->param = st->param * 10 + *p - '0';
        p++;
        break;
      default:
        st->repeat_count = st->param;
        if (st->repeat_count == 0) {
          st->repeat_count = 1;
        }
        st->state = PS_DECSIXEL;
        st->param = 0;
        st->nparams = 0;
        break;
      }
      break;

    case PS_DECGCI:
      /* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
      switch (*p) {
      case '\x1b':
        st->state = PS_ESC;
        p++;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        st->param = st->param * 10 + *p - '0';
        p++;
        break;
      case ';':
        if (st->nparams < DECSIXEL_PARAMS_MAX) {
          st->params[st->nparams++] = st->param;
        }
        st->param = 0;
        p++;
        break;
      default:
        st->state = PS_DECSIXEL;
        if (st->nparams < DECSIXEL_PARAMS_MAX) {
          st->params[st->nparams++] = st->param;
        }
        st->param = 0;

        if (st->nparams > 0) {
          st->color_index = st->params[0];
          if (st->color_index < 0) {
            st->color_index = 0;
          } else if (st->color_index >= 256) {
            st->color_index = 256 - 1;
          }
        }

        if (st->nparams > 4) {
          if (st->params[1] == 1) {
            /* HLS */
            if (st->params[2] > 360) {
              st->params[2] = 360;
            }
            if (st->params[3] > 100) {
              st->params[3] = 100;
            }
            if (st->params[4] > 100) {
              st->params[4] = 100;
            }
            image->palette[st->color_index]
              = hls_to_rgb(st->params[2], st->params[3], st->params[4]);
          } else if (st->params[1] == 2) {
            /* RGB */
            if (st->params[2] > 100) {
              st->params[2] = 100;
            }
            if (st->params[3] > 100) {
              st->params[3] = 100;
            }
            if (st->params[4] > 100) {
              st->params[4] = 100;
            }
            image->palette[st->color_index]
              = SIXEL_XRGB(st->params[2], st->params[3], st->params[4]);
          }
        }
        break;
      }
      break;
    default:
      break;
    }
  }

  status = (0);

end:
  return status;
}
