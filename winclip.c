// winclip.c (part of mintty)
// Copyright 2008-10  Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "charset.h"
#include "child.h"
#include "config.h"
#include "linedisc.h"

#include <winnls.h>
#include <richedit.h>
#include <shellapi.h>
#include <wtypes.h>
#include <objidl.h>
#include <oleidl.h>
#include <sys/cygwin.h>

static DWORD WINAPI
shell_exec_thread(void *data)
{
  wchar *wpath = data;
  if ((int)ShellExecuteW(wnd, 0, wpath, 0, 0, SW_SHOWNORMAL) <= 32) {
    uint error = GetLastError();
    if (error != ERROR_CANCELLED) {
      char msg[1024];
      FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | 64,
        0, error, 0, msg, sizeof msg, 0
      );
      MessageBox(0, msg, 0, 0);
    }
  }
  free(wpath);
  return 0;
}

static void
shell_exec(const wchar *wpath)
{
  CreateThread(0, 0, shell_exec_thread, (void *)wpath, 0, 0);
}

void
win_open(const wchar *wpath)
{
  const wchar *p = wpath;
  while (iswalpha(*p)) p++;
  
  if (*wpath == '\\' || *p == ':') {
    // Looks like it's a Windows path or URI
    shell_exec(wpath);
  }
  else {
    // Need to convert POSIX path to Windows first
    const wchar *conv_wpath = child_conv_path(wpath);
    free((void *)wpath);
    if (conv_wpath)
      shell_exec(conv_wpath);
  }
}


void
win_copy(const wchar *data, int *attr, int len)
{
  HGLOBAL clipdata, clipdata2, clipdata3 = 0;
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

  if (cfg.copy_as_rtf) {
    wchar unitab[256];
    char *rtf = null;
    uchar *tdata = (uchar *) lock2;
    wchar *udata = (wchar_t *) lock;
    int rtflen = 0, uindex = 0, tindex = 0;
    int rtfsize = 0;
    int multilen, blen, alen, totallen;
    char before[16], after[4];
    int fgcolour, lastfgcolour = 0;
    int bgcolour, lastbgcolour = 0;
    int attrBold, lastAttrBold = 0;
    int attrUnder, lastAttrUnder = 0;
    int palette[NALLCOLOURS];
    int numcolours;

    for (int i = 0; i < 256; i++)
      MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                          (char[]){i}, 1, unitab + i, 1);

    rtfsize = 100 + strlen(cfg.font.name);
    rtf = newn(char, rtfsize);
    rtflen =
      sprintf(rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
              cfg.font.name, cfg.font.size * 2);

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
      for (int i = 0; i < (len - 1); i++) {
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

        if (attr[i] & ATTR_INVISIBLE)
          fgcolour = bgcolour;

        palette[fgcolour]++;
        palette[bgcolour]++;
      }

     /*
      * Next - Create a reduced palette
      */
      numcolours = 0;
      for (int i = 0; i < NALLCOLOURS; i++) {
        if (palette[i] != 0)
          palette[i] = ++numcolours;
      }

     /*
      * Finally - Write the colour table
      */
      rtf = renewn(rtf, rtfsize + (numcolours * 25));
      strcat(rtf, "{\\colortbl ;");
      rtflen = strlen(rtf);

      for (int i = 0; i < NALLCOLOURS; i++) {
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

     /* Skip carriage returns */
      if (tdata[tindex] == '\r')
        tindex++, uindex++;

     /*
      * Set text attributes, if any, except on newlines
      */
      if (attr && tdata[tindex] != '\n') {
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

        if (attr[tindex] & ATTR_INVISIBLE)
          fgcolour = bgcolour;

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
      for (int i = 0; i < multilen; i++) {
        if (tdata[tindex + i] == '\\' || tdata[tindex + i] == '{' ||
            tdata[tindex + i] == '}')
          totallen += 2;
        else if (tdata[tindex + i] == 0x0A)
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
      for (int i = 0; i < multilen; i++) {
        if (tdata[tindex + i] == '\\' || tdata[tindex + i] == '{' ||
            tdata[tindex + i] == '}') {
          rtf[rtflen++] = '\\';
          rtf[rtflen++] = tdata[tindex + i];
        }
        else if (tdata[tindex + i] == 0x0A) {
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
  }

  GlobalUnlock(clipdata);
  GlobalUnlock(clipdata2);

  SendMessage(wnd, WM_IGNORE_CLIP, true, 0);

  if (OpenClipboard(wnd)) {
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
  SendMessage(wnd, WM_IGNORE_CLIP, false, 0);
}

static void
paste_hdrop(HDROP drop)
{
  uint buf_len = 32, buf_pos = 0;
  char *buf = newn(char, buf_len);
  void buf_add(char c) {
    if (buf_pos >= buf_len)
      buf = renewn(buf, buf_len *= 2);
    buf[buf_pos++] = c;
  }

#if CYGWIN_VERSION_API_MINOR >= 222
  // Update Cygwin locale to terminal locale.
  cygwin_internal(CW_INT_SETLOCALE);
#endif
  uint n = DragQueryFileW(drop, -1, 0, 0);
  for (uint i = 0; i < n; i++) {
    
#if CYGWIN_VERSION_DLL_MAJOR >= 1007
    uint wfn_len = DragQueryFileW(drop, i, 0, 0);
    wchar wfn[wfn_len + 1];
    DragQueryFileW(drop, i, wfn, wfn_len + 1);
    char *fn = cygwin_create_path(CCP_WIN_W_TO_POSIX, wfn);
#else
    uint wfn_len = DragQueryFileA(drop, i, 0, 0);
    char wfn[wfn_len + 1];
    DragQueryFileA(drop, i, wfn, wfn_len + 1);
    char *fn = newn(char, MAX_PATH);
    cygwin_conv_to_full_posix_path(wfn, fn);
#endif

    bool has_tick = false, needs_quotes = false, needs_dollar = false;
    for (char *p = fn; *p && !needs_dollar; p++) {
      uchar c = *p;
      has_tick |= c == '\'';
      needs_quotes |= isascii(c) && !isalnum(c) && !strchr("+,-./@_~'", c);
      needs_dollar = iscntrl(c) || (needs_quotes && has_tick);
    }
    needs_quotes |= needs_dollar;
    
    if (needs_dollar)
      buf_add('$');
    if (needs_quotes)
      buf_add('\'');
    else if (*fn == '~')
      buf_add('\\');
    for (char *p = fn; *p; p++) {
      uchar c = *p;
      if (iscntrl(c)) {
        buf_add('\\');
        buf_add('0' + (c >> 6));
        buf_add('0' + (c >> 3 & 7));
        buf_add('0' + (c & 7));
      }
      else {
        if (c == '\'')
          buf_add('\\');
        buf_add(c);
      }
    }
    if (needs_quotes)
      buf_add('\'');
    buf_add(' ');  // Filename separator
    free(fn);
  }
  buf_pos--;  // Drop trailing space
  ldisc_send(buf, buf_pos, false);
  free(buf);
}

static void
paste_unicode_text(HANDLE data)
{
  wchar *s = GlobalLock(data);
  uint l = wcslen(s);
  term_paste(s, l);
  GlobalUnlock(data);
}

static void
paste_text(HANDLE data)
{
  char *cs = GlobalLock(data);
  uint l = MultiByteToWideChar(CP_ACP, 0, cs, -1, 0, 0) - 1;
  wchar s[l];
  MultiByteToWideChar(CP_ACP, 0, cs, -1, s, l);
  GlobalUnlock(data);
  term_paste(s, l);
}

void
win_paste(void)
{
  if (!OpenClipboard(null))
    return;  
  HGLOBAL data;
  term_deselect();
  if ((data = GetClipboardData(CF_HDROP)))
    paste_hdrop(data);
  else if ((data = GetClipboardData(CF_UNICODETEXT)))
    paste_unicode_text(data);
  else if ((data = GetClipboardData(CF_TEXT)))
    paste_text(data);
  CloseClipboard();
}

static volatile long dt_ref_count;

static FORMATETC dt_format = { 0, null, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

static __stdcall HRESULT
dt_query_interface(IDropTarget *this, REFIID iid, void **p)
{
  if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDropTarget)) {
    InterlockedIncrement(&dt_ref_count);
    *p = this;
    return S_OK;
  }  
  else {
    *p = null;
    return E_NOINTERFACE;
  }
}

static __stdcall ULONG
dt_add_ref(IDropTarget *unused(this))
{ return InterlockedIncrement(&dt_ref_count); }

static __stdcall ULONG
dt_release(IDropTarget *unused(this))
{ return InterlockedDecrement(&dt_ref_count); }

static __stdcall HRESULT
dt_drag_over(IDropTarget *unused(this),
             DWORD keys, POINTL unused(pos), DWORD *effect_p)
{
  switch (dt_format.cfFormat) {
    when CF_TEXT or CF_UNICODETEXT:
      *effect_p =
        *effect_p & (keys & MK_CONTROL ? DROPEFFECT_COPY : DROPEFFECT_MOVE)
        ?: *effect_p & (DROPEFFECT_COPY | DROPEFFECT_MOVE);
    when CF_HDROP:
      *effect_p &= DROPEFFECT_LINK;
    otherwise:
      *effect_p = DROPEFFECT_NONE;
  }
  return S_OK;
}
  
static bool
try_format(IDataObject *obj, CLIPFORMAT format)
{
  dt_format.cfFormat = format; 
  return obj->lpVtbl->QueryGetData(obj, &dt_format) == S_OK;
}

static __stdcall HRESULT
dt_drag_enter(IDropTarget *this, IDataObject *obj,
             DWORD keys, POINTL pos, DWORD *effect_p)
{
  try_format(obj, CF_HDROP) ||
  try_format(obj, CF_UNICODETEXT) ||
  try_format(obj, CF_TEXT) ||
  (dt_format.cfFormat = 0);
  return dt_drag_over(this, keys, pos, effect_p);
}  
  
static __stdcall HRESULT
dt_drag_leave(IDropTarget *unused(this))
{ return S_OK; }

static __stdcall HRESULT
dt_drop(IDropTarget *this, IDataObject *obj,
        DWORD keys, POINTL pos, DWORD *effect_p)
{ 
  dt_drag_enter(this, obj, keys, pos, effect_p);
  if (!effect_p)
    return 0;
  STGMEDIUM stgmed;
  if (obj->lpVtbl->GetData(obj, &dt_format, &stgmed) != S_OK)
    return 0;
  HGLOBAL data = stgmed.hGlobal;
  if (!data)
    return 0;
  switch (dt_format.cfFormat) {
    when CF_TEXT: paste_text(stgmed.hGlobal);
    when CF_UNICODETEXT: paste_unicode_text(data);
    when CF_HDROP: paste_hdrop(data);
  }
  return 0;
}

static IDropTargetVtbl
dt_vtbl = {
  dt_query_interface, dt_add_ref, dt_release,
  dt_drag_enter, dt_drag_over, dt_drag_leave, dt_drop
};

static IDropTarget dt = { &dt_vtbl };

void
win_init_drop_target(void)
{
  OleInitialize(null);
  RegisterDragDrop(wnd, &dt);
}
