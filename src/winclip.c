// winclip.c (part of mintty)
// Copyright 2008-12 Andy Koppe, 2018 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"
#include "termpriv.h"  // term_get_html
#include "charset.h"
#include "child.h"
#include "res.h"  // DIALOG_CLASS

#include <winnls.h>
#include <richedit.h>
#include <shellapi.h>
#include <wtypes.h>
#include <objidl.h>
#include <oleidl.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>  // cygwin_internal
#endif

static DWORD WINAPI
shell_exec_thread(void *data)
{
  wchar *wpath = data;

#ifdef __CYGWIN__
  /* Need to sync the Windows environment */
  cygwin_internal(CW_SYNC_WINENV);
#endif

  if ((INT_PTR)ShellExecuteW(wnd, 0, wpath, 0, 0, SW_SHOWNORMAL) <= 32) {
    uint error = GetLastError();
    if (error != ERROR_CANCELLED) {
      int msglen = 1024;
      wchar * msg = newn(wchar, msglen);
      FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | 64,
        0, error, 0, msg, msglen, 0
      );
      wchar sep[] = W("\n");
      msg = renewn(msg, wcslen(msg) + wcslen(sep) + wcslen(wpath) + 1);
      wcscat(msg, sep);
      wcscat(msg, wpath);
      message_box_w(0, msg, null, MB_ICONERROR, null);
    }
  }
  free(wpath);
  return 0;
}

void
shell_exec(wstring wpath)
// frees wpath
{
  CreateThread(0, 0, shell_exec_thread, (void *)wpath, 0, 0);
  // this frees wpath via shell_exec_thread
}

#define dont_debug_wsl

static struct {
  string p;
  wstring w;
} lxss_mounts [] = {
  {"cache", W("cache")},
  {"data", W("data")},
  {"home", W("home")},
  {"mnt", W("mnt")},
  {"root", W("root")},
};

static bool
ispathprefix(string pref, string path)
{
  if (*pref == '/')
    pref++;
  if (*path == '/')
    path++;
  int len = strlen(pref);
  if (0 == strncmp(pref, path, len)) {
    path += len;
    if (!*path || *path == '/')
      return true;
  }
  return false;
}

static bool
ispathprefixw(wstring pref, wstring path)
{
  if (*pref == '/')
    pref++;
  if (*path == '/')
    path++;
  int len = wcslen(pref);
  if (0 == wcsncmp(pref, path, len)) {
    path += len;
    if (!*path || *path == '/')
      return true;
  }
  return false;
}

static char * wsl_conf_mnt = 0;
static struct {
  char * dev_fs;
  char * mount_point;
} * wsl_fstab = 0;
static int wsl_fstab_len = 0;

static char *
skip(char * s)
{
  while (iswspace(*s))
    s++;
  return s;
}

static bool
wslmntmapped(void)
{
  static bool wslmnt_init = false;
  if (!wslmnt_init && *wsl_basepath) {
    char linebuf[222];
    char * rootfs = path_win_w_to_posix(wsl_basepath);
    char * wsl$conf = asform("%s/etc/wsl.conf", rootfs);
    char * fstab = asform("%s/etc/fstab", rootfs);

    FILE * conf = fopen(wsl$conf, "r");
    if (conf) {
      bool automount = false;
      while (fgets(linebuf, sizeof linebuf, conf)) {
        char * p = skip(linebuf);
        if (*p == '[') {
          automount = false;
          if (0 == strncasecmp(p, "[automount]", 11)) {
            p = skip(p + 11);
            if (!*p)
              automount = true;
          }
        }
        else if (automount && !strncasecmp(p, "root", 4)) {
          p = skip(p + 4);
          if (*p == '=') {
            p = skip(p + 1);
            char * x = strchr(p, '\n');
            if (x) {
              *x = 0;
              if (x-- > p && *x == '/')
                *x = 0;
            }
            wsl_conf_mnt = strdup(p);
          }
        }
      }
      fclose(conf);
    }

    FILE * mtab = fopen(fstab, "r");
    if (mtab) {
      while (fgets(linebuf, sizeof linebuf, mtab)) {
        char * p1 = skip(linebuf);
        if (*p1 != '#') {
          char * x = p1;
          while (!iswspace(*x))
            x++;
          *x++ = 0;
          char * p2 = skip(x);
          x = p2;
          while (!iswspace(*x))
            x++;
          *x = 0;
          if (x-- > p2 && *x == '/')
            *x = 0;
          if (*p1 && *p2) {
            wsl_fstab = renewn(wsl_fstab, wsl_fstab_len + 1);
            wsl_fstab[wsl_fstab_len].dev_fs = strdup(p1);
            wsl_fstab[wsl_fstab_len].mount_point = strdup(p2);
            wsl_fstab_len++;
          }
        }
      }
      fclose(mtab);
    }

    free(fstab);
    free(wsl$conf);
    free(rootfs);
    wslmnt_init = true;
  }
  return wsl_conf_mnt || wsl_fstab;
}

static char *
unwslpath(wchar * wpath)
{
  char * res = null;
  if (wslmntmapped()) {
    char * wslpath = cs__wcstoutf(wpath);
    if (wsl_conf_mnt && ispathprefix(wsl_conf_mnt, wslpath))
      res = asform("/cygdrive%s", &wslpath[strlen(wsl_conf_mnt)]);
    else
      for (int i = 0; i < wsl_fstab_len; i++)
        if (ispathprefix(wsl_fstab[i].mount_point, wslpath))
          if (wsl_fstab[i].dev_fs[1] == ':') {
            res = asform("/cygdrive/%c%s", 
                         tolower(wsl_fstab[i].dev_fs[0]), 
                         &wslpath[strlen(wsl_fstab[i].mount_point)]);
            break;
          }
    free(wslpath);
  }
  return res;
}

static char *
wslpath(char * path)
{
  char * res = null;
  if (ispathprefix("/cygdrive", path) && wslmntmapped()) {
    path += 10;  // point to drive letter
    if (wsl_conf_mnt)
      res = asform("%s/%s", wsl_conf_mnt, path);
    else if (path[1] == '/')
      for (int i = 0; i < wsl_fstab_len; i++)
        if (tolower(*path) == tolower(wsl_fstab[i].dev_fs[0])
         && wsl_fstab[i].dev_fs[1] == ':'
         && wsl_fstab[i].dev_fs[2] == 0) {
            path ++;
            res = asform("%s%s", wsl_fstab[i].mount_point, path);
            break;
          }
  }
  // cygwin-internal paths not supported
  return res;
}

wchar *
dewsl(wchar * wpath)
{
#ifdef debug_wsl
  printf("open <%ls>\n", wpath);
#endif
  char * unwslp = unwslpath(wpath);
  if (unwslp) {
    wchar * unwsl = cs__utftowcs(unwslp);
    free(unwslp);
    delete(wpath);
    wpath = unwsl;
  }
  else if (wcsncmp(wpath, W("/mnt/"), 5) == 0) {
    wchar * unwsl = newn(wchar, wcslen(wpath) + 6);
    wcscpy(unwsl, W("/cygdrive"));
    wcscat(unwsl, wpath + 4);
    delete(wpath);
    wpath = unwsl;
  }
  else if (*wpath == '/' && *wsl_basepath) {
    static wchar * wbase = 0;
    if (!wbase) {
      char * pbase = path_win_w_to_posix(wsl_basepath);
      wbase = cs__mbstowcs(pbase);
      free(pbase);
    }

    wchar * unwsl = newn(wchar, wcslen(wbase) + wcslen(wpath) + 1);
    wcscpy(unwsl, wbase);
    wcscat(unwsl, wpath);
    delete(wpath);
    wpath = unwsl;
  }
  else if (*wpath == '/') {  // prepend %LOCALAPPDATA%\lxss[\rootfs]
    // deprecated case; for WSL, wsl_basepath should be set
    char * appd = getenv("LOCALAPPDATA");
    if (appd) {
      wchar * wappd = cs__mbstowcs(appd);
      appd = path_win_w_to_posix(wappd);
      free(wappd);
      wappd = cs__mbstowcs(appd);
      free(appd);

      bool rootfs_mount = true;
      for (uint i = 0; i < lengthof(lxss_mounts); i++) {
        if (ispathprefixw(lxss_mounts[i].w, wpath)) {
          rootfs_mount = false;
          break;
        }
      }

      wchar * unwsl = newn(wchar, wcslen(wappd) + wcslen(wpath) + 13);
      wcscpy(unwsl, wappd);
      free(wappd);
      wcscat(unwsl, W("/lxss"));
      if (rootfs_mount)
        wcscat(unwsl, W("/rootfs"));
      wcscat(unwsl, wpath);
      delete(wpath);
      wpath = unwsl;
    }
  }
  return wpath;
}

void
win_open(wstring wpath, bool adjust_dir)
// frees wpath
{
  // unescape
  int wl = wcslen(wpath);
  if ((*wpath == '"' || *wpath == '\'') && wpath[wl - 1] == *wpath) {
    // don't wpath++ (later delete!)
    wchar * p0 = (wchar *)wpath;
    for (int i = 0; i < wl - 2; i++)
      p0[i] = p0[i + 1];
    p0[wl - 2] = 0;
  }
  else {
    wchar * p0 = (wchar *)wpath;
    wchar * p1 = p0;
    while (*p0) {
      if (*p0 == '\\')
        p0++;
      if (*p0)
        *p1++ = *p0++;
    }
    *p1 = 0;
  }

  wstring p = wpath;
  while (iswalpha(*p)) p++;
  if (*wpath == '\\' || *p == ':' || wcsncmp(W("www."), wpath, 4) == 0) {
    // Looks like it's a Windows path or URI
    shell_exec(wpath); // frees wpath
  }
  else {
    // Need to convert POSIX path to Windows first
    if (support_wsl) {
      // First, we need to replicate some of the handling of relative paths 
      // as implemented in child_conv_path,
      // because the dewsl functionality would actually go in between 
      // the workflow of child_conv_path.
      // We cannot determine the WSL foreground process and its 
      // current directory, so we can only consider the working directory 
      // explicitly communicated via the OSC 7 escape sequence here.
      if (*wpath != '/' && wcsncmp(wpath, W("~/"), 2) != 0) {
        if (child_dir && *child_dir) {
          wchar * cd = cs__mbstowcs(child_dir);
          cd = renewn(cd, wcslen(cd) + wcslen(wpath) + 2);
          cd[wcslen(cd)] = '/';
          wcscpy(&cd[wcslen(cd) + 1], wpath);
          delete(wpath);
          wpath = cd;
        }
      }

      wpath = dewsl((wchar *)wpath);
    }
    wstring conv_wpath = child_conv_path(wpath, adjust_dir);
#ifdef debug_wsl
    printf("open <%ls> <%ls>\n", wpath, conv_wpath);
#endif
    delete(wpath);
    if (conv_wpath)
      shell_exec(conv_wpath); // frees conv_wpath
    else
      message_box(0, strerror(errno), null, MB_ICONERROR, null);
  }
}

// Convert RGB24 to xterm-256 8-bit value (always >= 16)
// For simplicity, assume RGB space is perceptually uniform.
// There are 5 places where one of two outputs needs to be chosen when the
// input is the exact middle:
// - The r/g/b channels and the gray value: the higher value output is chosen.
// - If the gray and color have same distance from the input - color is chosen.
static uchar
rgb_to_x256(uchar r, uchar g, uchar b)
{
    // Calculate the nearest 0-based color index at 16 .. 231
#   define v2ci(v) (v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40)
    int ir = v2ci(r), ig = v2ci(g), ib = v2ci(b);   // 0..5 each
#   define color_index() (36 * ir + 6 * ig + ib)  /* 0..215, lazy evaluation */

    // Calculate the nearest 0-based gray index at 232 .. 255
    int average = (r + g + b) / 3;
    int gray_index = average > 238 ? 23 : (average - 3) / 10;  // 0..23

    // Calculate the represented colors back from the index
    static const int i2cv[6] = {0, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
    int cr = i2cv[ir], cg = i2cv[ig], cb = i2cv[ib];  // r/g/b, 0..255 each
    int gv = 8 + 10 * gray_index;  // same value for r/g/b, 0..255

    // Return the one which is nearer to the original input rgb value
#   define dist_square(A,B,C, a,b,c) ((A-a)*(A-a) + (B-b)*(B-b) + (C-c)*(C-c))
    int color_err = dist_square(cr, cg, cb, r, g, b);
    int gray_err  = dist_square(gv, gv, gv, r, g, b);
    return color_err <= gray_err ? 16 + color_index() : 232 + gray_index;
}

static cattrflags
apply_attr_colour_rtf(cattr ca, attr_colour_mode mode, int * pfgi, int * pbgi)
{
  ca = apply_attr_colour(ca, mode);
  *pfgi = (ca.attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
  *pbgi = (ca.attr & ATTR_BGMASK) >> ATTR_BGSHIFT;
  // For ACM_RTF_GEN: COLOUR_NUM means "no colour" (-1)
  if (*pfgi == COLOUR_NUM && mode == ACM_RTF_GEN) *pfgi = -1;
  if (*pbgi == COLOUR_NUM && mode == ACM_RTF_GEN) *pbgi = -1;

  if (CCL_TRUEC(*pfgi))
    *pfgi = rgb_to_x256(red(ca.truefg), green(ca.truefg), blue(ca.truefg));
  if (CCL_TRUEC(*pbgi))
    *pbgi = rgb_to_x256(red(ca.truebg), green(ca.truebg), blue(ca.truebg));

  return ca.attr;
}

void
win_copy(const wchar *data, cattr *cattrs, int len)
{
  win_copy_as(data, cattrs, len, 0);
}

void
win_copy_as(const wchar *data, cattr *cattrs, int len, char what)
{
  //printf("win_copy %d '%c'\n", len, what);
  HGLOBAL clipdata, clipdata2, clipdata3 = 0;
  int len2;
  void *lock, *lock2, *lock3;

  len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, null, null);

  clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len * sizeof(wchar));
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

  memcpy(lock, data, len * sizeof(wchar));
  WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, null, null);

  if (cattrs && ((cfg.copy_as_rtf && !what) || what == 'r')) {
    wchar unitab[256];
    char *rtf = null;
    uchar *tdata = (uchar *) lock2;
    wchar *udata = (wchar *) lock;
    int rtflen = 0, uindex = 0, tindex = 0;
    int rtfsize = 0;
    int multilen, blen, alen, totallen;
    char before[16], after[4];
    int fgcolour, lastfgcolour = 0;
    int bgcolour, lastbgcolour = 0;
    int attrBold, lastAttrBold = 0;
    int attrUnder, lastAttrUnder = 0;
    int attrItalic, lastAttrItalic = 0;
    int attrStrikeout, lastAttrStrikeout = 0;
    int attrHidden, lastAttrHidden = 0;
    int palette[COLOUR_NUM];
    int numcolours;

    for (int i = 0; i < 256; i++)
      MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                          (char[]){i}, 1, unitab + i, 1);

    wstring cfgfont = *cfg.copy_as_rtf_font ? cfg.copy_as_rtf_font : cfg.font.name;
    int cfgsize = cfg.copy_as_rtf_font_size ? cfg.copy_as_rtf_font_size : cfg.font.size;
    char * rtffontname = newn(char, wcslen(cfgfont) * 9 + 1);
    char * rtffnpoi = rtffontname;
    for (uint i = 0; i < wcslen(cfgfont); i++)
      if (!(cfgfont[i] & 0xFF80) && !strchr("\\;{}", cfgfont[i]))
        *rtffnpoi++ = cfgfont[i];
      else
        rtffnpoi += sprintf(rtffnpoi, "\\u%d '", cfgfont[i]);
    *rtffnpoi = '\0';
    rtfsize = 100 + strlen(rtffontname);
    rtf = newn(char, rtfsize);
    rtflen = sprintf(rtf,
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern\\fprq1 %s;}}\\f0\\fs%d",
      rtffontname, cfgsize * 2);
    free(rtffontname);

   /*
    * Add colour palette
    * {\colortbl;\red255\green0\blue0;\red0\green0\blue128;...}
    */

   /*
    * First - Determine all colours in use
    *    o  Foreground and background colours share the same palette
    */
    memset(palette, 0, sizeof(palette));
    for (int i = 0; i < (len - 1); i++) {
      apply_attr_colour_rtf(cattrs[i], ACM_RTF_PALETTE, &fgcolour, &bgcolour);
      palette[fgcolour]++;
      palette[bgcolour]++;
    }

   /*
    * Next - Create a reduced palette
    */
    numcolours = 0;
    for (int i = 0; i < COLOUR_NUM; i++) {
      if (palette[i] != 0)
        palette[i] = ++numcolours;
    }

   /*
    * Finally - Write the colour table
    */
    rtf = renewn(rtf, rtfsize + (numcolours * 25));
    strcat(rtf, "{\\colortbl;");
    rtflen = strlen(rtf);

    for (int i = 0; i < COLOUR_NUM; i++) {
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
      if (tdata[tindex] != '\n') {

        uint attr = cattrs[uindex].attr;

        if (rtfsize < rtflen + 64) {
          rtfsize = rtflen + 512;
          rtf = renewn(rtf, rtfsize);
        }

       /*
        * Determine foreground and background colours
        */
        attr = apply_attr_colour_rtf(cattrs[uindex], ACM_RTF_GEN, &fgcolour, &bgcolour);
        attrBold = attr & ATTR_BOLD;
        attrUnder = attr & UNDER_MASK;
        attrItalic = attr & ATTR_ITALIC;
        attrStrikeout = attr & ATTR_STRIKEOUT;
        attrHidden = attr & ATTR_INVISIBLE;

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

        if (lastAttrItalic != attrItalic) {
          lastAttrItalic = attrItalic;
          rtflen += sprintf(&rtf[rtflen], "%s", attrItalic ? "\\i " : "\\i0 ");
        }

        if (lastAttrUnder != attrUnder) {
          lastAttrUnder = attrUnder;
          rtflen +=
            sprintf(&rtf[rtflen], "%s", attrUnder ? "\\ul " : "\\ulnone ");
        }

        if (lastAttrStrikeout != attrStrikeout) {
          lastAttrStrikeout = attrStrikeout;
          rtflen += sprintf(&rtf[rtflen], "%s", attrStrikeout ? "\\strike " : "\\strike0 ");
        }

        if (lastAttrHidden != attrHidden) {
          lastAttrHidden = attrHidden;
          rtflen += sprintf(&rtf[rtflen], "%s", attrHidden ? "\\v " : "\\v0 ");
        }
      }

      if (unitab[tdata[tindex]] == udata[uindex]) {
        multilen = 1;
        before[0] = '\0';
        after[0] = '\0';
        blen = alen = 0;
      }
      else {
        multilen = WideCharToMultiByte(CP_ACP, 0, &udata[uindex], 1,
                                                  null, 0, null, null);
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
        else if (tdata[tindex + i] == '\n')
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
        else if (tdata[tindex + i] == '\n') {
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

  //printf("OpenClipboard win_copy\n");
  if (OpenClipboard(wnd)) {
    clipboard_token = true;
    EmptyClipboard();
    // copy clipboard text formats
    SetClipboardData(CF_UNICODETEXT, clipdata);
    SetClipboardData(CF_TEXT, clipdata2);
    // copy clipboard RTF format
    if (clipdata3)
      SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
    // determine HTML format level requested
    int level = 0;
    if (cfg.copy_as_html && !what)
      //level = cfg.copy_as_rtf ? 2 : 3;
      level = cfg.copy_as_html;
    else if (what == 'h')
      level = 1;
    else if (what == 'f')
      level = 2;
    else if (what == 'H')
      level = 3;
    // copy clipboard HTML format
    UINT CF_HTML = level ? RegisterClipboardFormatA("HTML Format") : 0;
    if (CF_HTML) {
      char * html = term_get_html(level);
      char * htmlpre = "<html><!--StartFragment-->";
      char * htmlpost = "<!--EndFragment--></html>";
      int htmldescrlen = 92;
      char * htmlcb = asform(
             "Version:0.9\n"
             "StartHTML:%08d\n"
             "EndHTML:%08d\n"
             "StartFragment:%08d\n"
             "EndFragment:%08d\n"
             "%s%s%s",
             htmldescrlen,
             htmldescrlen + strlen(htmlpre) + strlen(html) + strlen(htmlpost),
             htmldescrlen + strlen(htmlpre),
             htmldescrlen + strlen(htmlpre) + strlen(html),
             htmlpre, html, htmlpost);
      free(html);
      int len = strlen(htmlcb);
      //printf("clipboard HTML Format:\n%s\n", htmlcb);
      HGLOBAL clipdatahtml = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len);
      char * cliphtml = GlobalLock(clipdatahtml);
      if (cliphtml) {
        memcpy(cliphtml, htmlcb, len);
        free(htmlcb);
        GlobalUnlock(clipdatahtml);
        SetClipboardData(CF_HTML, clipdatahtml);
      }
      else {
        GlobalFree(clipdatahtml);
      }
    }

    CloseClipboard();
  }
  else {
    GlobalFree(clipdata);
    GlobalFree(clipdata2);
  }
}

static uint buf_len, buf_pos;
static char * buf;

static void buf_init()
{
  buf_len = 32;
  buf_pos = 0;
  buf = newn(char, buf_len);
}

static void
buf_add(char c)
{
  if (buf_pos >= buf_len)
    buf = renewn(buf, buf_len *= 2);
  buf[buf_pos++] = c;
}

static void
buf_path(wchar * wfn, bool convert, bool quote)
{
    bool posix_path = convert || support_wsl;
    char * fn = posix_path
              ? path_win_w_to_posix(wfn)
              : cs__wcstoutf(wfn);

    bool has_tick = false, needs_quotes = false, needs_dollar = false;
    for (char *p = fn; *p && !needs_dollar; p++) {
      uchar c = *p;
      has_tick |= c == '\'';
      if (posix_path || !strchr("\\:", c))
        needs_quotes |= isascii(c) && !isalnum(c) && !strchr("+,-./@_~'", c);
      needs_dollar = iscntrl(c) || (needs_quotes && has_tick);
    }
    needs_quotes |= needs_dollar;
    needs_quotes &= quote;

    if (needs_dollar)
      buf_add('$');
    if (needs_quotes)
      buf_add(posix_path ? '\'' : '"');
    else if (*fn == '~')
      buf_add('\\');
    char *p = fn;
    if (support_wsl) {
#ifdef debug_wsl
      printf("paste <%s>\n", p);
#endif
      char * wslp = wslpath(p);
      if (wslp) {
        free(fn);
        fn = wslp;
        p = fn;
      }
      else if (*wsl_basepath) {
        static char * wsl_root = 0;
        if (!wsl_root) {
          wsl_root = path_win_w_to_posix(wsl_basepath);
        }
        // strip wsl_root
        int len = strlen(wsl_root);
        if (strncmp(wsl_root, p, len) == 0) {
          p += len;
          if (!*p) {
            p--;
            *p = '/';
          }
        }
        else if (strncmp(p, "/cygdrive/", 10) == 0) {
          // convert /cygdrive/X/path referring to mounted drive
          p += 5;
          //strncpy(p, "/mnt", 4);
          memcpy(p, "/mnt", 4);
        }
      }
      else {
        // check for prefix %LOCALAPPDATA%\lxss
        // deprecated case; for WSL, wsl_basepath should be set
        char * appd = getenv("LOCALAPPDATA");
        if (appd) {
          wchar * wappd = cs__mbstowcs(appd);
          appd = path_win_w_to_posix(wappd);
          free(wappd);
        }

        char * mount_point(char * path, char * appd) {
          if (!appd)
            return null;
          int lapp = strlen(appd);
          if (strncmp(path, appd, lapp) != 0)
            return null;
          // "$USERPROFILE/AppData/Local/xxx/yyy"
          path += strlen(appd);
          // "/xxx/yyy/zzz"
          if (!ispathprefix("lxss", path))
            return null;
          // "/lxss/yyy/zzz"
          path += 5;
          // "/yyy/zzz"
          for (uint i = 0; i < lengthof(lxss_mounts); i++) {
            if (ispathprefix(lxss_mounts[i].p, path)) {
              // "/home/zzz"
              return path;
            }
          }
          if (ispathprefix("rootfs", path)) {
            // "/rootfs/zzz"
            path += 7;
            // "/zzz"
            if (*path)
              return path;
            else
              return "/";
          }
          return null;
        }

        char * mp = mount_point(p, appd);
        if (mp)
          p = mp;
        else if (strncmp(p, "/cygdrive/", 10) == 0) {
          p += 5;
          //strncpy(p, "/mnt", 4);
          memcpy(p, "/mnt", 4);
        }
      }
    }
    for (; *p; p++) {
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
      buf_add(posix_path ? '\'' : '"');
    free(fn);
}

static void
paste_hdrop(HDROP drop)
{
#if CYGWIN_VERSION_API_MINOR >= 222
  // Update Cygwin locale to terminal locale.
  cygwin_internal(CW_INT_SETLOCALE);
#endif
  uint n = DragQueryFileW(drop, -1, 0, 0);

  void bufpaths(bool convert, bool quote) {
    buf_init();
    for (uint i = 0; i < n; i++) {
      uint wfn_len = DragQueryFileW(drop, i, 0, 0);
      wchar wfn[wfn_len + 1];
      DragQueryFileW(drop, i, wfn, wfn_len + 1);
#ifdef debug_dragndrop
      printf("dropped file <%ls>\n", wfn);
#endif
      if (i)
        buf_add(' ');  // Filename separator
      buf_path(wfn, convert, quote);
    }
    buf[buf_pos] = 0;
  }

  if (!support_wsl && *cfg.drop_commands) {
    // try to determine foreground program
    char * fg_prog = foreground_prog();
    if (fg_prog) {
      // match program base name
      char * drops = cs__wcstombs(cfg.drop_commands);
      char * paste = matchconf(drops, fg_prog);
      if (paste) {
        char * format = strchr(paste, '%');
        if (format && strchr("swSW", *(++format)) && !strchr(format, '%')) {
          switch (*format) {
            when 's': bufpaths(true, false);
            when 'S': bufpaths(true, true);
            when 'w': bufpaths(false, false);
            when 'W': bufpaths(false, true);
          }
          *format = 's';
          char * pastebuf = newn(char, strlen(paste) + strlen(buf) + 1);
          sprintf(pastebuf, paste, buf);
          child_send(pastebuf, strlen(pastebuf));
          free(pastebuf);
        }
        else
          child_send(paste, strlen(paste));
        free(drops);  // also frees paste which points into drops
        free(fg_prog);
        free(buf);
        return;
      }
      free(drops);
      free(fg_prog);
    }
  }

  bufpaths(true, true);

  if (term.bracketed_paste)
    child_write("\e[200~", 6);
  child_send(buf, buf_pos);
  free(buf);
  if (term.bracketed_paste)
    child_write("\e[201~", 6);
}

static void
paste_path(HANDLE data)
{
  wchar *s = GlobalLock(data);
  buf_init();
  buf_path(s, true, true);
  GlobalUnlock(data);

  if (term.bracketed_paste)
    child_write("\e[200~", 6);
  child_send(buf, buf_pos);
  free(buf);
  if (term.bracketed_paste)
    child_write("\e[201~", 6);
}

static void
paste_unicode_text(HANDLE data)
{
  wchar *s = GlobalLock(data);
  uint l = wcslen(s);
  term_paste(s, l, (GetKeyState(VK_CONTROL) & 0x80) != 0);
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
  term_paste(s, l, (GetKeyState(VK_CONTROL) & 0x80) != 0);
}

static void
do_win_paste(bool do_path)
{
  //printf("OpenClipboard win_paste\n");
  if (!OpenClipboard(null))
    return;

  if (cfg.input_clears_selection)
    term.selected = false;

  HGLOBAL data;
  if ((data = GetClipboardData(CF_HDROP))) {
    //printf("pasting CF_HDROP\n");
    paste_hdrop(data);
  }
  else if ((data = GetClipboardData(CF_UNICODETEXT))) {
    //printf("pasting CF_UNICODETEXT\n");
    if (do_path)
      paste_path(data);
    else
      paste_unicode_text(data);
  }
  else if ((data = GetClipboardData(CF_TEXT))) {
    //printf("pasting CF_TEXT\n");
    paste_text(data);
  }

  CloseClipboard();
}

void
win_paste(void)
{
  do_win_paste(false);
}

void
win_paste_path(void)
{
  do_win_paste(true);
}


/*
 *  Drag-and-drop
 */

static wchar *
paste_dialog(HANDLE data, CLIPFORMAT cf)
{
  if (cf == CF_UNICODETEXT) {
    //cf. paste_unicode_text(data);
    // used for URLs
    // used for data:... schemes (http://ciembor.github.io/4bit/#)
    wchar * s = wcsdup(GlobalLock(data));
    GlobalUnlock(data);
    return s;
  }
  else if (cf == CF_HDROP) {
    //cf. paste_hdrop(data);
    // used for filenames
    if (1 == DragQueryFileW(data, -1, 0, 0)) {
      uint wbuflen = DragQueryFileW(data, 0, 0, 0) + 1;
      wchar * wc = newn(wchar, wbuflen);
      DragQueryFileW(data, 0, wc, wbuflen);
      return wc;
    }
  }
  return null;
}

static volatile LONG dt_ref_count;

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
  // check whether drag-and-drop target is the terminal window
  // not the Options menu or any of its controls
  POINT p = {.x = pos.x, .y = pos.y};
  HWND h = WindowFromPoint(p);
  if (h == wnd) {
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
      when CF_TEXT: paste_text(data);
      when CF_UNICODETEXT: paste_unicode_text(data);
      when CF_HDROP: paste_hdrop(data);
    }
  }
  else {
    // support drag-and-drop to certain input fields
    char cn[10];
    HWND widget = null;
    // find the SendMessage target window
    while (h && (GetClassNameA(h, cn, sizeof(cn)), strcmp(cn, DIALOG_CLASS) != 0)) {
#ifdef debug_dragndrop
      printf("%8p (%s) ", h, cn);
#endif
      // pick up the actual drag-and-drop target widget
      if (strcmp(cn, "ComboBox") == 0 || strcmp(cn, "Button") == 0)
        widget = h;  // or unconditionally use the last before DIALOG_CLASS?
      h = GetParent(h);
    }
#ifdef debug_dragndrop
    printf("%8p (%s)\n", h, h ? cn : "");
#endif
    if (!h)
      return 0;

    dt_drag_enter(this, obj, keys, pos, effect_p);
    if (!effect_p)
      return 0;
    STGMEDIUM stgmed;
    if (obj->lpVtbl->GetData(obj, &dt_format, &stgmed) != S_OK)
      return 0;
    HGLOBAL data = stgmed.hGlobal;
    if (!data)
      return 0;

    wchar * drop = paste_dialog(data, dt_format.cfFormat);
    if (drop) {
      // this will only work with the DIALOG_CLASS target
      SendMessage(h, WM_USER, (WPARAM)widget, (LPARAM)drop);
      free(drop);
    }
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
