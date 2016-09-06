// winimg.c (part of mintty)
// Licensed under the terms of the GNU General Public License v3 or later.

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>   /* isdigit */
#include <string.h>  /* memcpy */
#include <windows.h>

#include "term.h"
#include "winpriv.h"
#include "termpriv.h"
#include "winimg.h"

// tempfile_t manipulation

static tempfile_t *tempfile_current = NULL;
static size_t tempfile_num = 0;
static size_t const TEMPFILE_MAX_SIZE = 1024 * 1024 * 16;  /* 16MB */
static size_t const TEMPFILE_MAX_NUM = 16;

static tempfile_t *
tempfile_new(void)
{
  tempfile_t *tempfile;
  FILE *fp;

  fp = tmpfile();
  if (!fp)
    return NULL;

  tempfile = malloc(sizeof(tempfile_t));
  if (!tempfile)
    return NULL;

  tempfile->fp = fp;
  tempfile->ref_counter = 1;

  tempfile_num++;

  return tempfile;
}

static void
tempfile_destroy(tempfile_t *tempfile)
{
  if (tempfile == tempfile_current)
    tempfile_current = NULL;
  fclose(tempfile->fp);
  free(tempfile);
  tempfile_num--;
}

static void
tempfile_ref(tempfile_t *tempfile)
{
  tempfile->ref_counter++;
}

static void
tempfile_deref(tempfile_t *tempfile)
{
  if (--tempfile->ref_counter == 0)
    tempfile_destroy(tempfile);
}

static size_t
tempfile_size(tempfile_t *tempfile)
{
  fpos_t fsize = 0;

  fseek(tempfile->fp, 0L, SEEK_END);
  fgetpos(tempfile->fp, &fsize);

  return (size_t)fsize;
}

static tempfile_t *
tempfile_get(void)
{
  size_t size;

  if (!tempfile_current) {
    tempfile_current = tempfile_new();
    return tempfile_current;
  }

  /* get file size */
  size = tempfile_size(tempfile_current);

  /* if the file size reaches TEMPFILE_MAX_SIZE, return new temporary file */
  if (size > TEMPFILE_MAX_SIZE) {
    tempfile_current = tempfile_new();
    return tempfile_current;
  }

  /* increment reference counter */
  tempfile_ref(tempfile_current);

  return tempfile_current;
}

static bool
tempfile_write(tempfile_t *tempfile, unsigned char *p, size_t pos, size_t size)
{
  size_t nbytes;

  fseek((FILE *)tempfile->fp, pos, SEEK_SET);
  nbytes = fwrite(p, 1, size, tempfile->fp);
  if (nbytes != size)
    return false;

  return true;
}

static bool
tempfile_read(tempfile_t *tempfile, unsigned char *p, size_t pos, size_t size)
{
  size_t nbytes;

  fflush((FILE *)tempfile->fp);
  fseek((FILE *)tempfile->fp, pos, SEEK_SET);
  nbytes = fread(p, 1, size, (FILE *)tempfile->fp);
  if (nbytes != size)
    return false;

  return true;
}

// temp_strage_t implementation

static temp_strage_t *
strage_create(void)
{
  temp_strage_t *strage;
  tempfile_t *tempfile;

  tempfile = tempfile_get();
  if (!tempfile)
    return NULL;

  strage = malloc(sizeof(temp_strage_t));
  if (!strage)
    return NULL;

  strage->tempfile = tempfile;
  strage->position = tempfile_size(strage->tempfile);

  return strage;
}

static void
strage_destroy(temp_strage_t *strage)
{
  tempfile_deref(strage->tempfile);
  free(strage);
}

static bool
strage_write(temp_strage_t *strage, unsigned char *p, size_t size)
{
  return tempfile_write(strage->tempfile, p, strage->position, size);
}

static bool
strage_read(temp_strage_t *strage, unsigned char *p, size_t size)
{
  return tempfile_read(strage->tempfile, p, strage->position, size);
}

bool
winimg_new(imglist **ppimg, unsigned char *pixels,
           int left, int top, int width, int height,
           int pixelwidth, int pixelheight)
{
  imglist *img;

  img = (imglist *)malloc(sizeof(imglist));
  if (!img)
    return false;

  img->pixels = pixels;
  img->hdc = NULL;
  img->hbmp = NULL;
  img->left = left;
  img->top = top;
  img->width = width;
  img->height = height;
  img->pixelwidth = pixelwidth;
  img->pixelheight = pixelheight;
  img->next = NULL;
  img->strage = NULL;

  *ppimg = img;

  return true;
}

// create DC handle if it is not initialized, or resume from hibernate
void
winimg_lazyinit(imglist *img)
{
  BITMAPINFO bmpinfo;
  unsigned char *pixels;
  HDC dc;
  size_t size;

  if (img->hdc)
    return;

  size = img->pixelwidth * img->pixelheight * 4;

  dc = GetDC(wnd);

  bmpinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmpinfo.bmiHeader.biWidth = img->pixelwidth;
  bmpinfo.bmiHeader.biHeight = - img->pixelheight;
  bmpinfo.bmiHeader.biPlanes = 1;
  bmpinfo.bmiHeader.biBitCount = 32;
  bmpinfo.bmiHeader.biCompression = BI_RGB;
  img->hdc = CreateCompatibleDC(dc);
  img->hbmp = CreateDIBSection(dc, &bmpinfo, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
  SelectObject(img->hdc, img->hbmp);
  if (img->pixels) {
    CopyMemory(pixels, img->pixels, size);
    free(img->pixels);
  } else {
    // resume from hibernation
    assert(img->strage);
    strage_read(img->strage, pixels, size);
  }
  img->pixels = pixels;

  ReleaseDC(wnd, dc);
}

// serialize an image into a temp file to save the memory
static void
winimg_hibernate(imglist *img)
{
  size_t size;
  temp_strage_t *strage;

  size = img->pixelwidth * img->pixelheight * 4;

  if (!img->hdc)
    return;

  strage = strage_create();
  if (!strage)
    return;

  if (!strage_write(strage, img->pixels, size)) {
    strage_destroy(strage);
    return;
  }

  // delete allocated DIB section.
  DeleteDC(img->hdc);
  DeleteObject(img->hbmp);
  img->pixels = NULL;
  img->hdc = NULL;
  img->hbmp = NULL;

  img->strage = strage;
}

void
winimg_destroy(imglist *img)
{
  if (img->hdc) {
    DeleteDC(img->hdc);
    DeleteObject(img->hbmp);
  } else if (img->pixels) {
    free(img->pixels);
  } else {
    strage_destroy(img->strage);
  }
  free(img);
}

void
winimgs_clear(void)
{
  imglist *img, *prev;

  // clear images in current screen
  for (img = term.imgs.first; img; ) {
    prev = img;
    img = img->next;
    winimg_destroy(prev);
  }

  // clear images in alternate screen
  for (img = term.imgs.altfirst; img; ) {
    prev = img;
    img = img->next;
    winimg_destroy(prev);
  }

  term.imgs.first = NULL;
  term.imgs.last = NULL;
  term.imgs.altfirst = NULL;
  term.imgs.altlast = NULL;
}

void
winimg_paint(void)
{
  imglist *img;
  imglist *prev = NULL;
  int left, top;
  int x, y;
  termchar *dchar;
  bool update_flag;
  HDC dc;
  RECT rc;

  /* free disk space if number of tempfile exceeds TEMPFILE_MAX_NUM */
  while (tempfile_num > TEMPFILE_MAX_NUM && term.imgs.first) {
    img = term.imgs.first;
    term.imgs.first = term.imgs.first->next;
    winimg_destroy(img);
  }

  dc = GetDC(wnd);

  GetClientRect(wnd, &rc);
  IntersectClipRect(dc, rc.left + PADDING, rc.top + PADDING,
                    rc.left + PADDING + term.cols * cell_width,
                    rc.top + PADDING + term.rows * cell_height);
  for (img = term.imgs.first; img;) {
    // if the image is out of scrollback, collect it
    if (img->top + img->height - term.virtuallines < - term.sblines) {
      if (img == term.imgs.first)
        term.imgs.first = img->next;
      if (img == term.imgs.last)
        term.imgs.last = prev;
      if (prev)
        prev->next = img->next;
      prev = img;
      img = img->next;
      winimg_destroy(prev);
    } else {
      // if the image is scrolled out, serialize it into a temp file.
      left = img->left;
      top = img->top - term.virtuallines - term.disptop;
      if (top + img->height < 0 || top > term.rows) {
        winimg_hibernate(img);
      } else {
        // create DC handle if it is not initialized, or resume from hibernate
        winimg_lazyinit(img);
        for (y = max(0, top); y < min(top + img->height, term.rows); ++y) {
          int wide_factor = (term.displines[y]->attr & LATTR_MODE) == LATTR_NORM ? 1: 2;
          for (x = left; x < min(left + img->width, term.cols); ++x) {
            dchar = &term.displines[y]->chars[x];

            // if sixel image is overwirtten by characters,
            // exclude the area from the clipping rect.
            update_flag = false;
            if (dchar->chr != SIXELCH)
              update_flag = true;
            if (dchar->attr.attr & (TATTR_RESULT| TATTR_CURRESULT))
              update_flag = true;
            if (term.selected && !update_flag) {
	      pos scrpos = {y + term.disptop, x};
              update_flag = term.sel_rect
                  ? posPle(term.sel_start, scrpos) && posPlt(scrpos, term.sel_end)
                  : posle(term.sel_start, scrpos) && poslt(scrpos, term.sel_end);
            }
            if (update_flag)
              ExcludeClipRect(dc,
                              x * wide_factor * cell_width + PADDING,
                              y * cell_height + PADDING,
                              (x + 1) * wide_factor * cell_width + 1,
                              (y + 1) * cell_height + 1);
          }
        }
        StretchBlt(dc, left * cell_width + PADDING, top * cell_height + PADDING,
                   img->width * cell_width, img->height * cell_height, img->hdc,
                   0, 0, img->pixelwidth, img->pixelheight, SRCCOPY);
      }
      prev = img;
      img = img->next;
    }
  }
  ReleaseDC(wnd, dc);
}
