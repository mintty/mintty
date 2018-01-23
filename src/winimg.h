#ifndef WINIMG_H
#define WINIMG_H

#include "config.h"

extern bool winimg_new(imglist **ppimg, unsigned char *pixels,
                       int top, int left, int width, int height,
                       int pixelwidth, int pixelheight);
extern void winimg_destroy(imglist *img);
extern void winimg_lazyinit(imglist *img);
extern void winimg_paint(void);
extern void winimgs_clear(void);

extern void win_emoji_show(int x, int y, wchar * efn, int elen, ushort lattr);

#endif
