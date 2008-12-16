// winclip.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "config.h"
#include "unicode.h"

#include <winnls.h>
#include <richedit.h>

void
win_write_clip(wchar * data, int *attr, int len)
{
  HGLOBAL clipdata, clipdata2, clipdata3;
  int len2;
  void *lock, *lock2, *lock3;

  len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, null, null);

  clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len * sizeof (wchar));
  clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

  if (!clipdata || !clipdata2) {
    if (clipdata)
      GlobalFree(clipdata);
    if (clipdata2)
      GlobalFree(clipdata2);
    return;
  }
  if (!(lock = GlobalLock(clipdata)))
    return;
  if (!(lock2 = GlobalLock(clipdata2)))
    return;

  memcpy(lock, data, len * sizeof (wchar));
  WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, null, null);

  wchar unitab[256];
  char *rtf = null;
  ubyte *tdata = (ubyte *) lock2;
  wchar *udata = (wchar_t *) lock;
  int rtflen = 0, uindex = 0, tindex = 0;
  int rtfsize = 0;
  int multilen, blen, alen, totallen, i;
  char before[16], after[4];
  int fgcolour, lastfgcolour = 0;
  int bgcolour, lastbgcolour = 0;
  int attrBold, lastAttrBold = 0;
  int attrUnder, lastAttrUnder = 0;
  int palette[NALLCOLOURS];
  int numcolours;

  get_unitab(CP_ACP, unitab, 0);

  rtfsize = 100 + strlen(cfg.font.name);
  rtf = newn(char, rtfsize);
  rtflen =
    sprintf(rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
            cfg.font.name, cfg.font.height * 2);

 /*
  * Add colour palette
  * {\colortbl ;\red255\green0\blue0;\red0\green0\blue128;}
  */

 /*
  * First - Determine all colours in use
  *    o  Foregound and background colours share the same palette
  */
  if (attr) {
    memset(palette, 0, sizeof (palette));
    for (i = 0; i < (len - 1); i++) {
      fgcolour = ((attr[i] & ATTR_FGMASK) >> ATTR_FGSHIFT);
      bgcolour = ((attr[i] & ATTR_BGMASK) >> ATTR_BGSHIFT);

      if (attr[i] & ATTR_REVERSE) {
        int tmpcolour = fgcolour;     /* Swap foreground and background */
        fgcolour = bgcolour;
        bgcolour = tmpcolour;
      }

      if (bold_mode == BOLD_COLOURS && (attr[i] & ATTR_BOLD)) {
        if (fgcolour < 8)     /* ANSI colours */
          fgcolour += 8;
        else if (fgcolour >= 256)     /* Default colours */
          fgcolour++;
      }

      if (attr[i] & ATTR_BLINK) {
        if (bgcolour < 8)     /* ANSI colours */
          bgcolour += 8;
        else if (bgcolour >= 256)     /* Default colours */
          bgcolour++;
      }

      palette[fgcolour]++;
      palette[bgcolour]++;
    }

   /*
    * Next - Create a reduced palette
    */
    numcolours = 0;
    for (i = 0; i < NALLCOLOURS; i++) {
      if (palette[i] != 0)
        palette[i] = ++numcolours;
    }

   /*
    * Finally - Write the colour table
    */
    rtf = renewn(rtf, rtfsize + (numcolours * 25));
    strcat(rtf, "{\\colortbl ;");
    rtflen = strlen(rtf);

    for (i = 0; i < NALLCOLOURS; i++) {
      if (palette[i] != 0) {
        rtflen +=
          sprintf(&rtf[rtflen], "\\red%d\\green%d\\blue%d;",
                  GetRValue(colours[i]),
                  GetGValue(colours[i]),
                  GetBValue(colours[i]));
      }
    }
    strcpy(&rtf[rtflen], "}");
    rtflen++;
  }

 /*
  * We want to construct a piece of RTF that specifies the
  * same Unicode text. To do this we will read back in
  * parallel from the Unicode data in `udata' and the
  * non-Unicode data in `tdata'. For each character in
  * `tdata' which becomes the right thing in `udata' when
  * looked up in `unitab', we just copy straight over from
  * tdata. For each one that doesn't, we must WCToMB it
  * individually and produce a \u escape sequence.
  * 
  * It would probably be more robust to just bite the bullet
  * and WCToMB each individual Unicode character one by one,
  * then MBToWC each one back to see if it was an accurate
  * translation; but that strikes me as a horrifying number
  * of Windows API calls so I want to see if this faster way
  * will work. If it screws up badly we can always revert to
  * the simple and slow way.
  */
  while (tindex < len2 && uindex < len && tdata[tindex] && udata[uindex]) {
    if (tindex + 1 < len2 && tdata[tindex] == '\r' &&
        tdata[tindex + 1] == '\n') {
      tindex++;
      uindex++;
    }

   /*
    * Set text attributes
    */
    if (attr) {
      if (rtfsize < rtflen + 64) {
        rtfsize = rtflen + 512;
        rtf = renewn(rtf, rtfsize);
      }

     /*
      * Determine foreground and background colours
      */
      fgcolour = ((attr[tindex] & ATTR_FGMASK) >> ATTR_FGSHIFT);
      bgcolour = ((attr[tindex] & ATTR_BGMASK) >> ATTR_BGSHIFT);

      if (attr[tindex] & ATTR_REVERSE) {
        int tmpcolour = fgcolour;     /* Swap foreground and background */
        fgcolour = bgcolour;
        bgcolour = tmpcolour;
      }

      if (bold_mode == BOLD_COLOURS && (attr[tindex] & ATTR_BOLD)) {
        if (fgcolour < 8)     /* ANSI colours */
          fgcolour += 8;
        else if (fgcolour >= 256)     /* Default colours */
          fgcolour++;
      }

      if (attr[tindex] & ATTR_BLINK) {
        if (bgcolour < 8)     /* ANSI colours */
          bgcolour += 8;
        else if (bgcolour >= 256)     /* Default colours */
          bgcolour++;
      }

     /*
      * Collect other attributes
      */
      if (bold_mode != BOLD_COLOURS)
        attrBold = attr[tindex] & ATTR_BOLD;
      else
        attrBold = 0;

      attrUnder = attr[tindex] & ATTR_UNDER;

     /*
      * Reverse video
      *   o  If video isn't reversed, ignore colour attributes for default
      *      foregound or background.
      *   o  Special case where bolded text is displayed using the default
      *      foregound and background colours - force to bolded RTF.
      */
      if (!(attr[tindex] & ATTR_REVERSE)) {
        if (bgcolour >= 256)  /* Default color */
          bgcolour = -1;      /* No coloring */

        if (fgcolour >= 256) {        /* Default colour */
          if (bold_mode == BOLD_COLOURS && (fgcolour & 1) && bgcolour == -1)
            attrBold = ATTR_BOLD;     /* Emphasize text with bold attribute */

          fgcolour = -1;      /* No coloring */
        }
      }

     /*
      * Write RTF text attributes
      */
      if (lastfgcolour != fgcolour) {
        lastfgcolour = fgcolour;
        rtflen +=
          sprintf(&rtf[rtflen], "\\cf%d ",
                  (fgcolour >= 0) ? palette[fgcolour] : 0);
      }

      if (lastbgcolour != bgcolour) {
        lastbgcolour = bgcolour;
        rtflen +=
          sprintf(&rtf[rtflen], "\\highlight%d ",
                  (bgcolour >= 0) ? palette[bgcolour] : 0);
      }

      if (lastAttrBold != attrBold) {
        lastAttrBold = attrBold;
        rtflen += sprintf(&rtf[rtflen], "%s", attrBold ? "\\b " : "\\b0 ");
      }

      if (lastAttrUnder != attrUnder) {
        lastAttrUnder = attrUnder;
        rtflen +=
          sprintf(&rtf[rtflen], "%s", attrUnder ? "\\ul " : "\\ulnone ");
      }
    }

    if (unitab[tdata[tindex]] == udata[uindex]) {
      multilen = 1;
      before[0] = '\0';
      after[0] = '\0';
      blen = alen = 0;
    }
    else {
      multilen =
        WideCharToMultiByte(CP_ACP, 0, unitab + uindex, 1, null, 0, null,
                            null);
      if (multilen != 1) {
        blen = sprintf(before, "{\\uc%d\\u%d", multilen, udata[uindex]);
        alen = 1;
        strcpy(after, "}");
      }
      else {
        blen = sprintf(before, "\\u%d", udata[uindex]);
        alen = 0;
        after[0] = '\0';
      }
    }
    assert(tindex + multilen <= len2);
    totallen = blen + alen;
    for (i = 0; i < multilen; i++) {
      if (tdata[tindex + i] == '\\' || tdata[tindex + i] == '{' ||
          tdata[tindex + i] == '}')
        totallen += 2;
      else if (tdata[tindex + i] == 0x0D || tdata[tindex + i] == 0x0A)
        totallen += 6;        /* \par\r\n */
      else if (tdata[tindex + i] > 0x7E || tdata[tindex + i] < 0x20)
        totallen += 4;
      else
        totallen++;
    }

    if (rtfsize < rtflen + totallen + 3) {
      rtfsize = rtflen + totallen + 512;
      rtf = renewn(rtf, rtfsize);
    }

    strcpy(rtf + rtflen, before);
    rtflen += blen;
    for (i = 0; i < multilen; i++) {
      if (tdata[tindex + i] == '\\' || tdata[tindex + i] == '{' ||
          tdata[tindex + i] == '}') {
        rtf[rtflen++] = '\\';
        rtf[rtflen++] = tdata[tindex + i];
      }
      else if (tdata[tindex + i] == 0x0D || tdata[tindex + i] == 0x0A) {
        rtflen += sprintf(rtf + rtflen, "\\par\r\n");
      }
      else if (tdata[tindex + i] > 0x7E || tdata[tindex + i] < 0x20) {
        rtflen += sprintf(rtf + rtflen, "\\'%02x", tdata[tindex + i]);
      }
      else {
        rtf[rtflen++] = tdata[tindex + i];
      }
    }
    strcpy(rtf + rtflen, after);
    rtflen += alen;

    tindex += multilen;
    uindex++;
  }

  rtf[rtflen++] = '}';        /* Terminate RTF stream */
  rtf[rtflen++] = '\0';
  rtf[rtflen++] = '\0';

  clipdata3 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, rtflen);
  if (clipdata3 && (lock3 = GlobalLock(clipdata3)) != null) {
    memcpy(lock3, rtf, rtflen);
    GlobalUnlock(clipdata3);
  }
  free(rtf);

  GlobalUnlock(clipdata);
  GlobalUnlock(clipdata2);

  SendMessage(hwnd, WM_IGNORE_CLIP, true, 0);

  if (OpenClipboard(hwnd)) {
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, clipdata);
    SetClipboardData(CF_TEXT, clipdata2);
    if (clipdata3)
      SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
    CloseClipboard();
  }
  else {
    GlobalFree(clipdata);
    GlobalFree(clipdata2);
  }

  SendMessage(hwnd, WM_IGNORE_CLIP, false, 0);
}

void
win_read_clip(wchar ** p, int *len)
{
  static HGLOBAL clipdata = null;
  static wchar *converted = 0;
  wchar *p2;

  if (converted) {
    free(converted);
    converted = 0;
  }
  if (!p) {
    if (clipdata)
      GlobalUnlock(clipdata);
    clipdata = null;
    return;
  }
  else if (OpenClipboard(null)) {
    if ((clipdata = GetClipboardData(CF_UNICODETEXT))) {
      CloseClipboard();
      *p = GlobalLock(clipdata);
      if (*p) {
        for (p2 = *p; *p2; p2++);
        *len = p2 - *p;
        return;
      }
    }
    else if ((clipdata = GetClipboardData(CF_TEXT))) {
      char *s;
      int i;
      CloseClipboard();
      s = GlobalLock(clipdata);
      i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
      *p = converted = newn(wchar, i);
      MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, converted, i);
      *len = i - 1;
      return;
    }
    else
      CloseClipboard();
  }

  *p = null;
  *len = 0;
}
