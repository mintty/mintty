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
#include "sixel.h"

// tempfile_t manipulation

static tempfile_t *tempfile_current = NULL;
static size_t tempfile_num = 0;
static size_t const TEMPFILE_MAX_SIZE = 1024 * 1024 * 16;  /* 16MB */
static size_t const TEMPFILE_MAX_NUM = 16;

// protection limit against exhaustion of resources (Windows handles)
// if we'd create ~10000 "CompatibleDCs", handle handling will fail...
static int cdc = 999;



#if CYGWIN_VERSION_API_MINOR >= 74

// GDI+ handling
static IStream * (WINAPI * pSHCreateMemStream)(void *, UINT) = 0;

#include <w32api/gdiplus/gdiplus.h>
#include <w32api/gdiplus/gdiplusflat.h>

#define dont_debug_gdiplus

#ifdef debug_gdiplus
static void
gpcheck(char * tag, GpStatus s)
{
  static char * gps[] = {
    "Ok",
    "GenericError",
    "InvalidParameter",
    "OutOfMemory",
    "ObjectBusy",
    "InsufficientBuffer",
    "NotImplemented",
    "Win32Error",
    "WrongState",
    "Aborted",
    "FileNotFound",
    "ValueOverflow",
    "AccessDenied",
    "UnknownImageFormat",
    "FontFamilyNotFound",
    "FontStyleNotFound",
    "NotTrueTypeFont",
    "UnsupportedGdiplusVersion",
    "GdiplusNotInitialized",
    "PropertyNotFound",
    "PropertyNotSupported",
    "ProfileNotFound",
  };
  if (s)
    printf("[%s] %d %s\n", tag, s, s >= 0 && s < lengthof(gps) ? gps[s] : "?");
}
#else
#define gpcheck(tag, s)	(void)s
#endif

static void
gdiplus_init(void)
{
  GpStatus s;
  static GdiplusStartupInput gi = (GdiplusStartupInput){1, NULL, FALSE, FALSE};
  static ULONG_PTR gis = 0;
  if (!gis) {
    s = GdiplusStartup(&gis, &gi, NULL);
    gpcheck("startup", s);

    HMODULE shc = GetModuleHandleA("shlwapi");
    pSHCreateMemStream = (void *)GetProcAddress(shc, "SHCreateMemStream");
  }
}

#endif


static tempfile_t *
tempfile_new(void)
{
  FILE *fp = tmpfile();
  if (!fp)
    return NULL;

  tempfile_t *tempfile = malloc(sizeof(tempfile_t));
  //printf("tempfile alloc %d -> %p\n", (int)sizeof(tempfile_t), tempfile);
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
  //printf("free tempfile %p\n", tempfile);
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
  fseek(tempfile->fp, 0L, SEEK_END);
  fpos_t fsize = 0;
  fgetpos(tempfile->fp, &fsize);

  return (size_t)fsize;
}

static tempfile_t *
tempfile_get(void)
{
  if (!tempfile_current) {
    tempfile_current = tempfile_new();
    return tempfile_current;
  }

  /* get file size */
  size_t size = tempfile_size(tempfile_current);

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
  fseek((FILE *)tempfile->fp, pos, SEEK_SET);
  size_t nbytes = fwrite(p, 1, size, tempfile->fp);
  if (nbytes != size)
    return false;

  return true;
}

static bool
tempfile_read(tempfile_t *tempfile, unsigned char *p, size_t pos, size_t size)
{
  fflush((FILE *)tempfile->fp);
  fseek((FILE *)tempfile->fp, pos, SEEK_SET);
  size_t nbytes = fread(p, 1, size, (FILE *)tempfile->fp);
  if (nbytes != size)
    return false;

  return true;
}

// temp_strage_t implementation

static temp_strage_t *
strage_create(void)
{
  tempfile_t *tempfile = tempfile_get();
  if (!tempfile)
    return NULL;

  temp_strage_t *strage = malloc(sizeof(temp_strage_t));
  //printf("strage alloc %d -> %p\n", (int)sizeof(temp_strage_t), strage);
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
  //printf("free strage %p\n", strage);
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


#define dont_debug_img_list
#define dont_debug_img_disp
#define dont_debug_img_over

static uint
winimg_len(imglist *img)
{
  return img->len ?: img->pixelwidth * img->pixelheight * 4;
}

bool
winimg_new(imglist **ppimg, char * id, unsigned char * pixels, uint len,
           int left, int top, int width, int height,
           int pixelwidth, int pixelheight, bool preserveAR,
           int crop_x, int crop_y, int crop_width, int crop_height,
           int attr)
{
  imglist *img = (imglist *)malloc(sizeof(imglist));
  //printf("winimg alloc %d -> [%d]\n", (int)sizeof(imglist), img->imgi);
  if (!img)
    return false;
#ifdef debug_img_list
  printf("winimg_new [%d]->%p l %d t %d w %d h %d\n", img->imgi, pixels, left, top, width, height);
#endif

  static int _imgi = 0;
#ifdef debug_img_disp
  printf("winimg_new %d @%d:%d\n", _imgi, left, top);
#endif
  img->imgi = _imgi++;

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
  img->prev = NULL;
  img->strage = NULL;
  img->attr = attr;

  img->len = len;
  if (len) {  // image format, not sixel
    img->id = id ? strdup(id) : 0;
    img->cwidth = cell_width;
    img->cheight = cell_height;

#if CYGWIN_VERSION_API_MINOR >= 74
    if (!pixelwidth || !pixelheight || preserveAR) {
      // determine pixelwidth and pixelheight from image
      uint pw, ph;
      gdiplus_init();
      GpStatus s;

      IStream * fs = pSHCreateMemStream(img->pixels, img->len);
      s = fs ? Ok : GenericError;
      gpcheck("create mem stream", s);

      GpBitmap * gbm = 0;
      s = GdipCreateBitmapFromStream(fs, &gbm);
      gpcheck("bitmap from stream", s);

      s = GdipGetImageWidth(gbm, &pw);
      gpcheck("get width", s);
      s = GdipGetImageHeight(gbm, &ph);
      gpcheck("get height", s);

      s = GdipDisposeImage(gbm);
      gpcheck("dispose image", s);

      if (fs) {
        // Release stream resources
        fs->lpVtbl->Release(fs);
      }

      if (s != Ok)
        return false;

      // cropping pre-adjustment
      if (crop_width > 0)
        pw = crop_width;
      else if (crop_width < 0) {
        crop_width = - crop_width;
        pw -= crop_width;
      }
      if (crop_height > 0)
        ph = crop_height;
      else if (crop_height < 0) {
        crop_height = - crop_height;
        ph -= crop_height;
      }
      if (crop_x)
        pw -= crop_x;
      if (crop_y)
        ph -= crop_y;
      if (crop_x || crop_y || crop_width || crop_height) {
        if (!crop_width)
          crop_width = pw - crop_x;
        if (!crop_height)
          crop_height = ph - crop_y;
      }

      if (pw <= 0 || ph <= 0)
        return false;

      img->crop_x = crop_x;
      img->crop_y = crop_y;
      img->crop_width = crop_width;
      img->crop_height = crop_height;

      /*
	case	given			effective
		w=	h=	pAR=	width	height
	0	x	y	0	x	y	(stretched)
	1	x	y	1	max x	max y	(adapt min)
	2	x	–	?	x	x * h / w
	3	–	y	?	y * w/h	y
	4	–	–	?	w	h
      */
      if (img->pixelwidth && img->pixelheight) {  // case 1
        if ((ulong)img->pixelwidth * (ulong)ph < (ulong)img->pixelheight * (ulong)pw) {
          img->pixelheight = (ulong)img->pixelwidth * (ulong)ph / pw;
          img->height = (img->pixelheight - 1) / cell_height + 1;
        }
        else if ((ulong)img->pixelheight * (ulong)pw < (ulong)img->pixelwidth * (ulong)ph) {
          img->pixelwidth = (ulong)img->pixelheight * (ulong)pw / ph;
          img->width = (img->pixelwidth - 1) / cell_width + 1;
        }
      }
      else if (img->pixelwidth) {  // case 2
        img->pixelheight = (ulong)img->pixelwidth * (ulong)ph / pw;
        img->height = (img->pixelheight - 1) / cell_height + 1;
      }
      else if (img->pixelheight) {  // case 3
        img->pixelwidth = (ulong)img->pixelheight * (ulong)pw / ph;
        img->width = (img->pixelwidth - 1) / cell_width + 1;
      }
      else {  // case 4
        img->pixelwidth = pw;
        img->width = (pw - 1) / cell_width + 1;
        img->pixelheight = ph;
        img->height = (ph - 1) / cell_height + 1;
      }
    }
#else
    (void)preserveAR;
    (void)crop_x, (void)crop_y, (void)crop_width, (void)crop_height;
#endif
  }
  else
    img->id = 0;

  *ppimg = img;

  return true;
}

// create DC handle if it is not initialized, or resume from hibernate
void
winimg_lazyinit(imglist *img)
{
  // non-sixel images are not pre-initialised
  if (img->len)
    return;

  if (img->hdc)
    return;

  if (!cdc)
    return;
#ifdef debug_dc
  printf("creating device context, capacity %d->\n", cdc);
#endif
  cdc--;

  HDC dc = GetDC(wnd);
  if (!dc)
    return;

  img->hdc = CreateCompatibleDC(dc);

  if (img->hdc) {
    BITMAPINFO bmpinfo;
    unsigned char *pixels;

    bmpinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpinfo.bmiHeader.biWidth = img->pixelwidth;
    bmpinfo.bmiHeader.biHeight = - img->pixelheight;
    bmpinfo.bmiHeader.biPlanes = 1;
    bmpinfo.bmiHeader.biBitCount = 32;
    bmpinfo.bmiHeader.biCompression = BI_RGB;
    bmpinfo.bmiHeader.biSizeImage = 0;
    img->hbmp = CreateDIBSection(dc, &bmpinfo, DIB_RGB_COLORS, (void*)&pixels, NULL, 0);
    if (img->hbmp) {
      /*HGDIOBJ res =*/
      SelectObject(img->hdc, img->hbmp);
      uint size = winimg_len(img);
      if (img->pixels) {
        CopyMemory(pixels, img->pixels, size);
        //printf("winimg_lazyinit free pixels [%d]->%p\n", img->imgi, img->pixels);
        free(img->pixels);
      } else {
        // resume from hibernation
        assert(img->strage);
        strage_read(img->strage, pixels, size);
      }
      //printf("winimg_lazyinit img->pixels = pixels [%d]->%p\n", img->imgi, pixels);
      img->pixels = pixels;
    }
  }

  ReleaseDC(wnd, dc);
}

// serialize an image into a temp file to save the memory
static void
winimg_hibernate(imglist *img)
{
  // non-sixel images are not paged out
  if (img->len)
    return;

  if (!img->hdc)
    return;

  temp_strage_t *strage = strage_create();
  //printf("winimg_hibernate [%d]->%p to %p\n", img->imgi, img->pixels, strage);
  if (!strage)
    return;

  if (!strage_write(strage, img->pixels, winimg_len(img))) {
    strage_destroy(strage);
    return;
  }
  img->strage = strage;

  // delete allocated DIB section.
  cdc++;
#ifdef debug_dc
  printf("release dc, capacity ->%d\n", cdc);
#endif
  DeleteDC(img->hdc);
  DeleteObject(img->hbmp);  // this also deallocates img->pixels
  img->hdc = NULL;
  img->hbmp = NULL;
  img->pixels = NULL;
}

void
winimg_destroy(imglist *img)
{
  if (img->hdc) {
    cdc++;
#ifdef debug_dc
  printf("release dc, capacity ->%d\n", cdc);
#endif
    DeleteDC(img->hdc);
    DeleteObject(img->hbmp);
  } else if (img->pixels) {
    //printf("winimg_destroy free pixels %p\n", img->pixels);
    free(img->pixels);
  } else {
    strage_destroy(img->strage);
  }
  if (img->id)
    free(img->id);
  free(img);
}

void
winimgs_clear(void)
{
  // clear parser state
  sixel_parser_deinit(term.imgs.parser_state);
  //printf("winimgs_clear free state %p\n", term.imgs.parser_state);
  free(term.imgs.parser_state);
  term.imgs.parser_state = NULL;

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

static void
draw_img(HDC dc, imglist * img)
{
#if CYGWIN_VERSION_API_MINOR >= 74
    gdiplus_init();

    GpStatus s;

    IStream * fs = pSHCreateMemStream(img->pixels, img->len);
    s = fs ? Ok : GenericError;
    gpcheck("create mem stream", s);

    GpImage * gimg = 0;
    if (s == Ok) {
      s = GdipLoadImageFromStream(fs, &gimg);
      gpcheck("load stream", s);
    }

    // position
    int left = img->left * cell_width;
    int top = (img->top - term.virtuallines - term.disptop) * cell_height;
    int width = img->pixelwidth;
    int height = img->pixelheight;
    left += PADDING;
    top += OFFSET + PADDING;

    int coord_transformed = 0;
    XFORM old_xform;
    if (img->cwidth != cell_width || img->cheight != cell_height) {
      coord_transformed = SetGraphicsMode(dc, GM_ADVANCED);
      if (coord_transformed && GetWorldTransform(dc, &old_xform)) {
        XFORM xform =
          (XFORM){(float)cell_width / (float)img->cwidth, 0.0,
                  0.0, (float)cell_height / (float)img->cheight,
                  left, top};
        coord_transformed = SetWorldTransform(dc, &xform);
        left = 0;
        top = 0;
      }
    }

    GpGraphics * gr;
    s = GdipCreateFromHDC(dc, &gr);
    gpcheck("create", s);

    if (coord_transformed)
      SetWorldTransform(dc, &old_xform);

#ifdef fill_bg_gdiplus
    // fill image area background (in case it's smaller or transparent);
    // background image brush should be used if configured;
    // that'll be tricky since we've transformed coordinates here,
    // so better do the padding before, in the GDI domain (FillRect below)
    GpSolidFill * br;
    colour bg = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
    //bg = RGB(90, 150, 222);  // test background filling
    s = GdipCreateSolidFill(0xFF000000 | red(bg) << 16 | green(bg) << 8 | blue(bg), &br);
    gpcheck("brush create", s);

    // determine area size for padding
    int awidth = img->width * img->cwidth;
    int aheight = img->height * img->cheight;
    s = GdipFillRectangleI(gr, br, left + width, top, awidth - width, height);
    gpcheck("brush fill", s);
    s = GdipFillRectangleI(gr, br, left, top + height, awidth, aheight - height);
    gpcheck("brush fill", s);

    s = GdipDeleteBrush(br);
    gpcheck("brush delete", s);
#endif

    GpImageAttributes * iattr = 0;
    //GdipCreateImageAttributes(&iattr);
#ifdef debug_cropping
    printf("draw %3d %3d %3d %3d crop %3d %3d %3d %3d\n", left, top, width, height, img->crop_x, img->crop_y, img->crop_width, img->crop_height);
#endif
    if (img->crop_x || img->crop_y || img->crop_width || img->crop_height)
      s = GdipDrawImageRectRectI(gr, gimg,
                                 left, top, width, height,
                                 img->crop_x, img->crop_y,
                                 img->crop_width, img->crop_height,
                                 UnitPixel, iattr, 0, 0);
    else
      s = GdipDrawImageRectI(gr, gimg, left, top, width, height);
    gpcheck("draw", s);
    //GdipDisposeImageAttributes(iattr);

    s = GdipFlush(gr, FlushIntentionFlush);
    gpcheck("flush", s);

    s = GdipDeleteGraphics(gr);
    gpcheck("delete gr", s);
    s = GdipDisposeImage(gimg);
    gpcheck("dispose img", s);

    if (fs) {
      // Release stream resources
      fs->lpVtbl->Release(fs);
    }
#else
  (void)dc; (void)img;
#endif
}

void
winimgs_paint(void)
{
  imglist * img;

  /* free disk space if number of tempfile exceeds TEMPFILE_MAX_NUM */
  while (tempfile_num > TEMPFILE_MAX_NUM && term.imgs.first) {
    img = term.imgs.first;
    term.imgs.first = term.imgs.first->next;
    term.imgs.first->prev = NULL;
    winimg_destroy(img);
  }

  HDC dc = GetDC(wnd);

  // clip off padding area, avoiding image artefacts when scrolling
  RECT rc;
  GetClientRect(wnd, &rc);
  IntersectClipRect(dc, rc.left + PADDING, rc.top + OFFSET + PADDING,
                    rc.left + PADDING + term.cols * cell_width,
                    rc.top + OFFSET + PADDING + term.rows * cell_height);

  // prepare detection of overwritten images for garbage collection
  bool drawn[term.rows * term.cols];
  memset(drawn, 0, sizeof drawn);

  // tame the flickering by backward traversal together with global clipping
  bool backward_img_traversal = true;
  img = backward_img_traversal ? term.imgs.last : term.imgs.first;
  imglist * next;
#ifdef debug_img_over
  printf("--------------- imglist loop\n");
#endif
  for (; img; img = next) {
    next = backward_img_traversal ? img->prev : img->next;

    // blink attribute
    if (term.blink_is_real && term.has_focus) {
      if (img->attr & ATTR_BLINK2) {
        if (term.tblinker2)
          continue;
      }
      else if (img->attr & ATTR_BLINK) {
        if (term.tblinker)
          continue;
      }
    }

    imglist * destrimg = 0;

    if (img->top + img->height - term.virtuallines < - term.sblines) {
      // if the image is out of scrollback, collect it
      destrimg = img;
#ifdef debug_img_list
      printf("paint: destroy @%d h %d virt %lld sb %d\n", img->top, img->height, term.virtuallines, term.sblines);
#endif
#ifdef debug_img_over
      printf("@%d:%d destroy out [%d]\n", img->top - term.virtuallines - term.disptop, img->left, img->imgi);
#endif
    } else {
      int left = img->left;
      int top = img->top - term.virtuallines - term.disptop;
      if (top + img->height < 0 || top > term.rows) {
        // if the image is scrolled out, serialize it into a temp file
#ifdef debug_img_list
        if (img->hdc)
          printf("paint: hibernate img [%d] v@%d s@%d\n", img->imgi, img->top, top);
#endif
        winimg_hibernate(img);
#ifdef debug_img_over
        printf("@%d:%d hiber [%d]\n", top, left, img->imgi);
#endif
      } else {
#ifdef debug_img_list
        printf("paint: check img [%d] v@%d s@%d\n", img->imgi, img->top, top);
#endif
        // create img DC handle if not initialized, or resume from hibernate
        winimg_lazyinit(img);

        // check all cells of image area;
        // overwritten cells are excluded from display,
        // if all cells are overwritten, flag for deletion
        bool disp_flag = false;
        for (int y = max(0, top); y < min(top + img->height, term.rows); ++y) {
          int wide_factor =
            (term.displines[y]->lattr & LATTR_MODE) == LATTR_NORM ? 1 : 2;
          for (int x = left; x < min(left + img->width, term.cols); ++x) {
            termchar *dchar = &term.displines[y]->chars[x];

            // if sixel image is overwritten by characters,
            // exclude the area from the clipping rect.
            bool clip_flag = false;
            if (dchar->chr != SIXELCH) {
              clip_flag = true;
              drawn[y * term.cols + x] = true;
            }
            else if (!drawn[y * term.cols + x]) {
              disp_flag = true;
              drawn[y * term.cols + x] = true;
#ifdef debug_img_disp
              printf("paint: dirty (%d) %d:%d %d >= %d\n", disp_flag, y, x, img->imgi, dchar->attr.imgi);
#endif
            }
            // if cell is overlaid by selection or cursor, exclude
            if (dchar->attr.attr & (TATTR_RESULT | TATTR_CURRESULT | TATTR_MARKED | TATTR_CURMARKED))
              clip_flag = true;
            if (term.selected && !clip_flag) {
              pos scrpos = {y + term.disptop, x, 0, 0, false};
              clip_flag = term.sel_rect
                  ? posPle(term.sel_start, scrpos) && posPlt(scrpos, term.sel_end)
                  : posle(term.sel_start, scrpos) && poslt(scrpos, term.sel_end);
            }
            if (clip_flag)
              ExcludeClipRect(dc,
                              x * wide_factor * cell_width + PADDING,
                              y * cell_height + OFFSET + PADDING,
                              (x + 1) * wide_factor * cell_width + PADDING,
                              (y + 1) * cell_height + OFFSET + PADDING);
          }
        }
#ifdef debug_img_over
        printf("@%d:%d disp [%sm%d[m [%d]\n", top, left, disp_flag ? "" : "41", disp_flag, img->imgi);
#endif

#ifdef scale_graphics_in_double_width_lines
#warning no working implementation yet; not done in xterm either
        termline * line = fetch_line(top);
        ushort lattr = line->lattr;
        release_line(line);
        if ((lattr & LATTR_MODE) != LATTR_NORM) {
          // fix position in double-width line:
          // adjust below: left, xlft, ...width, xrgt, iwidth, iheight
        }
#endif

        // fill image area background (in case it's smaller or transparent)
        // calculate area for padding
        int ytop = max(0, top) * cell_height + OFFSET + PADDING;
        int ybot = min(top + img->height, term.rows) * cell_height + OFFSET + PADDING;
        int xlft = left * cell_width + PADDING;
        int xrgt = min(left + img->width, term.cols) * cell_width + PADDING;
        if (img->len) {
          // better background handling implemented below; this version 
          // would expose artefacts if a transparent image is scrolled
        }
        else if (disp_flag) {
#ifdef debug_img_disp
          printf("paint: clear background\n");
#endif
          // determine image size for padding
          int iwidth;
          int iheight;
          if (img->len) {
            // image: actual picture size
            iwidth = img->pixelwidth * cell_width / img->cwidth;
            iheight = img->pixelheight * cell_height / img->cheight;
          }
          else {
            // sixel: actual picture size
            iwidth = img->cwidth * cell_width * img->width / img->pixelwidth;
            iheight = img->cheight * cell_height * img->height / img->pixelheight;
          }
          int ibot = max(0, top * cell_height + iheight) + OFFSET + PADDING;
          // fill either background image or colour
          if (*cfg.background) {
            fill_background(dc, &(RECT){xlft + iwidth, ytop, xrgt, ibot});
            fill_background(dc, &(RECT){xlft, ibot, xrgt, ybot});
          }
          else {
            colour bg = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
            //bg = RGB(90, 150, 222);  // test background filling
            HBRUSH br = CreateSolidBrush(bg);
            FillRect(dc, &(RECT){xlft + iwidth, ytop, xrgt, ibot}, br);
            FillRect(dc, &(RECT){xlft, ibot, xrgt, ybot}, br);
            DeleteObject(br);
          }
          if (!img->len) {
            // sixel needs this in addition
            ExcludeClipRect(dc, xlft + iwidth, ytop, xrgt, ibot);
            ExcludeClipRect(dc, xlft, ibot, xrgt, ybot);
          }
        }

        // now display, keep, or delete the image data
        if (disp_flag) {
#ifdef debug_img_list
          printf("paint: display img\n");
#endif
          if (img->len) {
            //draw_img(dc, img);
            // underlay image with background;
            // do it in a copy to avoid flickering
            HDC hdc = CreateCompatibleDC(dc);
            HBITMAP hbm = CreateCompatibleBitmap(dc, xrgt, ybot);
            (void)SelectObject(hdc, hbm);
            if (*cfg.background)
              fill_background(hdc, &(RECT){0, 0, xrgt, ybot});
            else {
              colour bg = colours[term.rvideo ? FG_COLOUR_I : BG_COLOUR_I];
              //bg = RGB(90, 150, 222);  // test background filling
              HBRUSH br = CreateSolidBrush(bg);
              FillRect(hdc, &(RECT){0, 0, xrgt, ybot}, br);
              DeleteObject(br);
            }
            draw_img(hdc, img);
            BitBlt(dc, xlft, ytop, xrgt - xlft, ybot - ytop,
                       hdc, xlft, ytop, SRCCOPY);
            DeleteObject(hbm);
            DeleteDC(hdc);
            ExcludeClipRect(dc, xlft, ytop, xrgt, ybot);
          }
          else {
            StretchBlt(dc,
                       left * cell_width + PADDING, top * cell_height + OFFSET + PADDING,
                       img->width * cell_width, img->height * cell_height,
                       img->hdc,
                       0, 0, img->pixelwidth, img->pixelheight, SRCCOPY);
            ExcludeClipRect(dc,
                       left * cell_width + PADDING, top * cell_height + OFFSET + PADDING,
                       left * cell_width + PADDING + img->width * cell_width,
                       top * cell_height + OFFSET + PADDING + img->height * cell_height
                       );
          }
          if (!backward_img_traversal) {
            // restore clipping region
            ReleaseDC(wnd, dc);
            dc = GetDC(wnd);
          }
        }
        else if (top < 0 || top + img->height > term.rows) {
          // we did not check the scrolled-out image part, 
          // so keep the image for later display (when scrolled-in again)
        }
        else {
          //destroy and remove
          destrimg = img;
#ifdef debug_img_list
          printf("paint: destroy @%d h %d virt %lld sb %d\n", img->top, img->height, term.virtuallines, term.sblines);
#endif
#ifdef debug_img_over
          printf("@%d:%d destroy over [%d]\n", top, left, img->imgi);
#endif
        }
      }
    }

    // proceed to next image in list; destroy current if requested
    if (destrimg) {
      if (img->next)
        img->next->prev = img->prev;
      else
        term.imgs.last = img->prev;
      if (img->prev)
        img->prev->next = img->next;
      else
        term.imgs.first = img->next;

      winimg_destroy(destrimg);
    }
  }

  ReleaseDC(wnd, dc);
}


#if CYGWIN_VERSION_API_MINOR >= 74

#include <fcntl.h>
#include "charset.h"  // path_win_w_to_posix

void
win_emoji_show(int x, int y, wchar * efn, void * * bufpoi, int * buflen, int elen, ushort lattr, bool italic)
{
  gdiplus_init();

  GpStatus s;

  bool use_stream = true;
  IStream * fs = 0;
  if (*bufpoi && pSHCreateMemStream) {  // use cached image data
    fs = pSHCreateMemStream(*bufpoi, *buflen);
    s = fs ? Ok : NotImplemented;
  }
  else if (use_stream) {
    s = GdipCreateStreamOnFile(efn, 0 /* FileMode.Open */, &fs);
    gpcheck("stream", s);
    if (s && pSHCreateMemStream) {
      char * fn = path_win_w_to_posix(efn);
      int f = open(fn, O_BINARY | O_RDONLY);
      free(fn);
      if (f) {
        struct stat stat;
        if (0 == fstat(f, &stat)) {
          char * img = newn(char, stat.st_size);
          int len;
          char * p = img;
          while ((len = read(f, p, stat.st_size - (p - img))) > 0) {
            p += len;
          }
          *bufpoi = img;
          *buflen = p - img;
          fs = pSHCreateMemStream(img, p - img);
          if (fs)
            s = Ok;
        }
        close(f);
      }
    }
  }
  else
    s = NotImplemented;

  GpImage * img = 0;
  if (s == Ok) {
    s = GdipLoadImageFromStream(fs, &img);
    gpcheck("load stream", s);
  }
  else {
    // This is reported to generate a memory leak, so rather use the stream.
    s = GdipLoadImageFromFile(efn, &img);
    gpcheck("load file", s);
  }

  int col = PADDING + x * cell_width;
  int row = OFFSET + PADDING + y * cell_height;
  if ((lattr & LATTR_MODE) >= LATTR_BOT)
    row -= cell_height;
  int w = elen * cell_width;
  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    w *= 2;
    // fix position in double-width line
    col += x * cell_width;
  }
  int h = cell_height;
  if ((lattr & LATTR_MODE) >= LATTR_TOP)
    h *= 2;
  // glitch: missing clipping for inconsistent double-height lines

  if (cfg.emoji_placement) {
    uint iw, ih;
    s = GdipGetImageWidth(img, &iw);
    gpcheck("width", s);
    s = GdipGetImageHeight(img, &ih);
    gpcheck("height", s);
    // consider aspect ratio so that ih/iw == h/w;
    // if EMPL_FULL, always adjust w
    // if ih/iw > h/w, make w smaller
    // if iw/ih > w/h, make h smaller
    if (cfg.emoji_placement == EMPL_FULL && ih * w != h * iw) {
      w = h * iw / ih;
    }
    else if (ih * w > h * iw) {
      int w0 = w;
      w = h * iw / ih;
      if (cfg.emoji_placement == EMPL_MIDDLE) {
        // horizontally center
        col += (w0 - w) / 2;
      }
    }
    else if (iw * h > w * ih) {
      int h0 = h;
      h = w * ih / iw;
      // vertically center
      row += (h0 - h) / 2;
    }
  }

  HDC dc = GetDC(wnd);

  int coord_transformed = 0;
  XFORM old_xform;
  coord_transformed = italic && SetGraphicsMode(dc, GM_ADVANCED);
  if (coord_transformed && GetWorldTransform(dc, &old_xform)) {
    //XFORM xform = (XFORM){1.0, 0.0, 1.1, 1.0, -1.1, 0.0};
    /* {eM11, eM12, eM21, eM22, eDx, eDy}
       y' = x * eM12 + y * eM22 + eDy
       x' = x * eM11 + y * eM21 + eDx
       x' = x + ( (r+h-1-y) / h) * w/4 - bottom-line_x-undent(w/8)
     */
    XFORM xform = (XFORM){1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    xform.eM21 = ((float)cell_width) * -0.25 / ((float)cell_height);
    //xform.eDx = ((float)cell_width) * 0.25 * ((float)(row - 1) / ((float)cell_height) + 1.0)
    //          - ((float)cell_width) * 0.125;
    xform.eDx = ((float)cell_width) * 
             (0.25 * ((float)(row - 1) / ((float)cell_height) + 1.0) - 0.125);
    coord_transformed = SetWorldTransform(dc, &xform);
  }

  GpGraphics * gr;
  s = GdipCreateFromHDC(dc, &gr);
  gpcheck("hdc", s);

  s = GdipDrawImageRectI(gr, img, col, row, w, h);
  gpcheck("draw", s);
  s = GdipFlush(gr, FlushIntentionFlush);
  gpcheck("flush", s);

  s = GdipDeleteGraphics(gr);
  gpcheck("delete gr", s);
  s = GdipDisposeImage(img);
  gpcheck("dispose img", s);

  if (coord_transformed)
    SetWorldTransform(dc, &old_xform);

  ReleaseDC(wnd, dc);

  if (fs) {
    // Release stream resources, close file.
    fs->lpVtbl->Release(fs);
  }
}

void
save_img(HDC dc, int x, int y, int w, int h, wstring fn)
{
  gdiplus_init();
  GpStatus s;

  if (!w || !h) {
    // determine size
    BITMAP bitmap;
    HGDIOBJ hbitmap = GetCurrentObject(dc, OBJ_BITMAP);
    GetObject(hbitmap, sizeof(BITMAP), &bitmap);
    //printf("%d %d\n", bitmap.bmHeight, bitmap.bmWidth);
    if (!w)
      w = bitmap.bmWidth - x;
    if (!h)
      h = bitmap.bmHeight - y;
  }

  // provide paintable bitmap of matching size
  HBITMAP copbm = CreateCompatibleBitmap(dc, w, h);
  HDC copdc = CreateCompatibleDC(dc);

  // copy contents into GDI+ bitmap
  (void)SelectObject(copdc, copbm);
  BitBlt(copdc, 0, 0, w, h, dc, x, y, SRCCOPY);
  GpImage* gimg;
  s = GdipCreateBitmapFromHBITMAP(copbm, 0, &gimg);
  gpcheck("create", s);
  DeleteDC(copdc);
  DeleteObject(copbm);

  // save as png
  CLSID png;
  CLSIDFromString(W("{557CF406-1A04-11D3-9A73-0000F81EF32E}"), &png);
  s = GdipSaveImageToFile(gimg, fn, &png, 0);
  gpcheck("save", s);

  s = GdipDisposeImage(gimg);
  gpcheck("dispose image", s);
}

#else

void
win_emoji_show(int x, int y, wchar * efn, void * * bufpoi, int * buflen, int elen, ushort lattr, bool italic)
{
  (void)x; (void)y;
  (void)efn; (void)bufpoi; (void)buflen;
  (void)elen; (void)lattr; (void)italic;
}

void
save_img(HDC dc, int x, int y, int w, int h, wstring fn)
{
  (void)dc;
  (void)x; (void)y; (void)w; (void)h;
  (void)fn;
}

#endif

