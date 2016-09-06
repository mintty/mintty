#ifndef WINIMG_H
#define WINIMG_H

#include "config.h"

bool
winimg_new(imglist **ppimg, unsigned char *pixels,
           int top, int left, int width, int height,
           int pixelwidth, int pixelheight);
void winimg_destroy(imglist *img);
void winimg_lazyinit(imglist *img);
void winimg_paint(void);
void winimgs_clear(void);

#endif
