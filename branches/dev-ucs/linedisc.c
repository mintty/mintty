// linedisc.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "linedisc.h"

#include "child.h"
#include "term.h"
#include "win.h"

#include "codepage.h"

/*
 * ldisc.c: PuTTY line discipline. Sits between the input coming
 * from keypresses in the window, and the output channel leading to
 * the back end. Implements echo and/or local line editing,
 * depending on what's currently configured.
 */

struct {
  char *buf;
  int buflen, bufsiz, quotenext;
} ldisc;

static int
uclen(uchar c)
{
  if ((c >= 32 && c <= 126) || (c >= 160))
    return 1;
  else if (c < 128)
    return 2;   /* ^x for some x */
  else
    return 4;   /* <XY> hex representation */
}

static void
ucwrite(uchar c)
{
  if ((c >= 32 && c <= 126) || c >= 160)
    term_write((char *) &c, 1);
  else if (c < 128) {
    char cc[2];
    cc[1] = (c == 127 ? '?' : c + 0x40);
    cc[0] = '^';
    term_write(cc, 2);
  }
  else {
    char cc[5];
    sprintf(cc, "<%02X>", c);
    term_write(cc, 4);
  }
}

static void
bsb(int n)
{
  while (n--)
    term_write("\010 \010", 3);
}

#define KCTRL(x) ((x^'@') | 0x100)

void
ldisc_init(void)
{
  ldisc.buf = null;
  ldisc.buflen = 0;
  ldisc.bufsiz = 0;
  ldisc.quotenext = 0;
}

void
ldisc_printf(bool interactive, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  char *s;
  int len = vasprintf(&s, fmt, va);
  va_end(va);
  if (len >= 0)
    ldisc_send(s, len, interactive);
  free(s);
}

void
ldisc_send(const char *buf, int len, bool interactive)
{
  if (interactive)
    term_deselect();
  
  int keyflag = 0;

 /*
  * Less than zero means null terminated special string.
  */
  if (len < 0) {
    len = strlen(buf);
    keyflag = KCTRL('@');
  }
 /*
  * Either perform local editing, or just send characters.
  */
  if (term.editing) {
    while (len--) {
      int c;
      c = *buf++ + keyflag;
      if (!interactive && c == '\r')
        c += KCTRL('@');
      switch (ldisc.quotenext ? ' ' : c) {
         /*
          * ^h/^?: delete, and output BSBs, to return to
          * last character boundary
          * ^w: delete, and output BSBs, to return to last
          * space/nonspace boundary
          * ^u: delete, and output BSBs, to return to BOL
          * ^c: Do a ^u then send a telnet IP
          * ^z: Do a ^u then send a telnet SUSP
          * ^\: Do a ^u then send a telnet ABORT
          * ^r: echo "^R\n" and redraw line
          * ^v: quote next char
          * ^d: if at BOL, end of file and close connection,
          * else send line and reset to BOL
          * ^m: send line-plus-\r\n and reset to BOL
          */
        when KCTRL('H') or KCTRL('?'):       /* backspace/delete */
          if (ldisc.buflen > 0) {
            if (term.echoing)
              bsb(uclen(ldisc.buf[ldisc.buflen - 1]));
            ldisc.buflen--;
          }
        when CTRL('W'):        /* delete word */
          while (ldisc.buflen > 0) {
            if (term.echoing)
              bsb(uclen(ldisc.buf[ldisc.buflen - 1]));
            ldisc.buflen--;
            if (ldisc.buflen > 0 && isspace((uchar) ldisc.buf[ldisc.buflen - 1])
                && !isspace((uchar) ldisc.buf[ldisc.buflen]))
              break;
          }
        when CTRL('R'):        /* redraw line */
          if (term.echoing) {
            term_write("^R\r\n", 4);
            for (int i = 0; i < ldisc.buflen; i++)
              ucwrite(ldisc.buf[i]);
          }
        when CTRL('V'):        /* quote next char */
          ldisc.quotenext = true;
        when CTRL('D'):        /* logout or send */
          if (ldisc.buflen != 0) {
            child_write(ldisc.buf, ldisc.buflen);
            ldisc.buflen = 0;
          }
        when KCTRL('M'):       /* send with newline */
          if (ldisc.buflen > 0)
            child_write(ldisc.buf, ldisc.buflen);
          else
            child_write("\r", 1);
          if (term.echoing)
            term_write("\r\n", 2);
          ldisc.buflen = 0;
        when CTRL('U')        /* delete line */
          or CTRL('C')        /* Send IP */
          or CTRL('\\')       /* Quit */
          or CTRL('Z'):       /* Suspend */
          while (ldisc.buflen > 0) {
            if (term.echoing)
              bsb(uclen(ldisc.buf[ldisc.buflen - 1]));
            ldisc.buflen--;
          }
        default:
          if (ldisc.buflen >= ldisc.bufsiz) {
            ldisc.bufsiz = ldisc.buflen + 256;
            ldisc.buf = renewn(ldisc.buf, ldisc.bufsiz);
          }
          ldisc.buf[ldisc.buflen++] = c;
          if (term.echoing)
            ucwrite((uchar) c);
          ldisc.quotenext = false;
      }
    }
  }
  else {
    if (ldisc.buflen != 0) {
      child_write(ldisc.buf, ldisc.buflen);
      while (ldisc.buflen > 0) {
        bsb(uclen(ldisc.buf[ldisc.buflen - 1]));
        ldisc.buflen--;
      }
    }
    if (len > 0) {
      if (term.echoing)
        term_write(buf, len);
      child_write(buf, len);
    }
  }
}

void
luni_send(const wchar *ws, int wlen, bool interactive)
{
  char s[wlen * MB_LEN_MAX];
  int len = cp_wcntombn(s, ws, sizeof s, wlen);
  if (len > 0)
    ldisc_send(s, len, interactive);
}
