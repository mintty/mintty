// termline.c (part of mintty)
// Copyright 2008-12 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"
#include "win.h"  // cfg.bidi


termline *
newline(int cols, int bce)
{
  termline *line = new(termline);
  line->chars = newn(termchar, cols);
  for (int j = 0; j < cols; j++)
    line->chars[j] = (bce ? term.erase_char : basic_erase_char);
  line->cols = line->size = cols;
  line->lattr = LATTR_NORM;
  line->temporary = false;
  line->cc_free = 0;
  return line;
}

void
freeline(termline *line)
{
  assert(line);
  free(line->chars);
  free(line);
}

/*
 * Compress and decompress a termline into an RLE-based format for
 * storing in scrollback. (Since scrollback almost never needs to
 * be modified and exists in huge quantities, this is a sensible
 * tradeoff, particularly since it allows us to continue adding
 * features to the main termchar structure without proportionally
 * bloating the terminal emulator's memory footprint unless those
 * features are in constant use.)
 */
struct buf {
  uchar *data;
  int len, size;
};

static void
add(struct buf *b, uchar c)
{
  assert(b);
  if (b->len >= b->size) {
    b->size = (b->len * 3 / 2) + 512;
    b->data = renewn(b->data, b->size);
  }
  b->data[b->len++] = c;
}

static int
get(struct buf *b)
{
  return b->data[b->len++];
}

/*
 * Add a combining character to a character cell.
 */
void
add_cc(termline *line, int col, wchar chr, cattr attr)
{
  assert(col >= 0 && col < line->cols);

 /*
  * Start by extending the cols array if the free list is empty.
  */
  if (!line->cc_free) {
    int n = line->size;
    line->size += 16 + (line->size - line->cols) / 2;
    line->chars = renewn(line->chars, line->size);
    line->cc_free = n;
    do
      line->chars[n].cc_next = 1;
    while (++n < line->size - 1);
    line->chars[n].cc_next = 0;  // Terminates the free list.
  }

 /*
  * Now walk the cc list of the cell in question.
  */
  while (line->chars[col].cc_next)
    col += line->chars[col].cc_next;

 /*
  * `col' now points at the last cc currently in this cell; so
  * we simply add another one.
  */
  int newcc = line->cc_free;
  if (line->chars[newcc].cc_next)
    line->cc_free = newcc + line->chars[newcc].cc_next;
  else
    line->cc_free = 0;
  line->chars[newcc].cc_next = 0;
  line->chars[newcc].chr = chr;
  line->chars[newcc].attr = attr;
  line->chars[col].cc_next = newcc - col;
}

/*
 * Clear the combining character list in a character cell.
 */
void
clear_cc(termline *line, int col)
{
  int oldfree, origcol = col;

  assert(col >= 0 && col < line->cols);

  if (!line->chars[col].cc_next)
    return;     /* nothing needs doing */

  oldfree = line->cc_free;
  line->cc_free = col + line->chars[col].cc_next;
  while (line->chars[col].cc_next)
    col += line->chars[col].cc_next;
  if (oldfree)
    line->chars[col].cc_next = oldfree - col;
  else
    line->chars[col].cc_next = 0;

  line->chars[origcol].cc_next = 0;
}

/*
 * Compare two character cells for equality. Special case required in
 * do_paint() where we override what we expect the chr and attr fields to be.
 */
int
termchars_equal_override(termchar *a, termchar *b, uint bchr, cattr battr)
{
 /* FULL-TERMCHAR */
  if (a->chr != bchr)
    return false;
  if ((a->attr.attr & ~DATTR_MASK) != (battr.attr & ~DATTR_MASK))
    return false;
  if (a->attr.truefg != battr.truefg)
    return false;
  if (a->attr.truebg != battr.truebg)
    return false;
  if (a->attr.ulcolr != battr.ulcolr)
    return false;
  while (a->cc_next || b->cc_next) {
    if (!a->cc_next || !b->cc_next)
      return false;     /* one cc-list ends, other does not */
    a += a->cc_next;
    b += b->cc_next;
    if (a->chr != b->chr)
      return false;
  }
  return true;
}

int
termchars_equal(termchar *a, termchar *b)
{
  return termchars_equal_override(a, b, b->chr, b->attr);
}

/*
 * Copy a character cell. (Requires a pointer to the destination termline,
 * so as to access its free list.)
 */
void
copy_termchar(termline *destline, int x, termchar *src)
{
  clear_cc(destline, x);

  destline->chars[x] = *src;    /* copy everything except cc-list */
  destline->chars[x].cc_next = 0;       /* and make sure this is zero */

  while (src->cc_next) {
    src += src->cc_next;
    add_cc(destline, x, src->chr, src->attr);
  }
}

/*
 * Move a character cell within its termline.
 */
void
move_termchar(termline * line, termchar *dest, termchar *src)
{
 /* First clear the cc list from the original char, just in case. */
  clear_cc(line, dest - line->chars);

 /* Move the character cell and adjust its cc_next. */
  *dest = *src; /* copy everything except cc-list */
  if (src->cc_next)
    dest->cc_next = src->cc_next - (dest - src);

 /* Ensure the original cell doesn't have a cc list. */
  src->cc_next = 0;
}

static void
makeliteral_chr(struct buf *buf, termchar *c)
{
 /*
  * The encoding for characters assigns one-byte codes to printable
  * ASCII characters and NUL, and two-byte codes to anything else up
  * to 0x96FF. UTF-16 surrogates also get two-byte codes, to avoid non-BMP
  * characters exploding to six bytes. Anything else is three bytes long.
  */
  wchar wc = c->chr;
  if (wc == 0 || (wc >= 0x20 && wc < 0x7F))
    ;
  else {
    uchar b = wc >> 8;
    if (b < 0x80)
      b += 0x80;
    else if (b < 0x97)
      b -= 0x7F;
    else if (b >= 0xD8 && b < 0xE0)
      b -= 0xC0;
    else
      add(buf, 0x7F);
    add(buf, b);
  }
  add(buf, wc);
}

static void
makeliteral_attr(struct buf *b, termchar *c)
{
 /*
  * My encoding for attributes is 16-bit-granular and assumes
  * that the top bit of the word is never required. I either
  * store a two-byte value with the top bit clear (indicating
  * just that value), or a four-byte value with the top bit set
  * (indicating the same value with its top bit clear).
  *
  * However, first I permute the bits of the attribute value, so
  * that the eight bits of colour (four in each of fg and bg)
  * which are never non-zero unless xterm 256-colour mode is in
  * use are placed higher up the word than everything else. This
  * ensures that attribute values remain 16-bit _unless_ the
  * user uses extended colour.
  */
  cattrflags attr = c->attr.attr & ~DATTR_MASK;
  uint truefg = c->attr.truefg;
  uint truebg = c->attr.truebg;
  colour ulcolr = c->attr.ulcolr;

  if (attr < 0x800000 && !truefg && !truebg && ulcolr != (colour)-1) {
    add(b, (uchar) ((attr >> 16) & 0xFF));
    add(b, (uchar) ((attr >> 8) & 0xFF));
    add(b, (uchar) (attr & 0xFF));
  }
  else {
    add(b, (uchar) ((attr >> 56) & 0xFF) | 0x80);
    add(b, (uchar) ((attr >> 48) & 0xFF));
    add(b, (uchar) ((attr >> 40) & 0xFF));
    add(b, (uchar) ((attr >> 32) & 0xFF));
    add(b, (uchar) ((attr >> 24) & 0xFF));
    add(b, (uchar) ((attr >> 16) & 0xFF));
    add(b, (uchar) ((attr >> 8) & 0xFF));
    add(b, (uchar) (attr & 0xFF));

    add(b, (uchar) ((truefg >> 16) & 0xFF));
    add(b, (uchar) ((truefg >> 8) & 0xFF));
    add(b, (uchar) (truefg & 0xFF));
    add(b, (uchar) ((truebg >> 16) & 0xFF));
    add(b, (uchar) ((truebg >> 8) & 0xFF));
    add(b, (uchar) (truebg & 0xFF));
    add(b, (uchar) ((ulcolr >> 16) & 0xFF));
    add(b, (uchar) ((ulcolr >> 8) & 0xFF));
    add(b, (uchar) (ulcolr & 0xFF));
  }
}

static void
makeliteral_cc(struct buf *b, termchar *c)
{
 /*
  * For combining characters, I just encode a bunch of ordinary
  * chars using makeliteral_chr, and terminate with a \0
  * character (which I know won't come up as a combining char
  * itself).
  */
  termchar z;

  while (c->cc_next) {
    c += c->cc_next;
    assert(c->chr != 0);
    makeliteral_chr(b, c);
    makeliteral_attr(b, c);
  }

  z.chr = 0;
  makeliteral_chr(b, &z);
}

static void
readliteral_chr(struct buf *buf, termchar *c, termline *unused(line))
{
  uchar b = get(buf);
  if (b == 0 || (b >= 0x20 && b < 0x7F))
    c->chr = b;
  else {
    if (b >= 0x80)
      b -= 0x80;
    else if (b < 0x18)
      b += 0x7F;
    else if (b < 0x20)
      b += 0xC0;
    else
      b = get(buf);
    c->chr = b << 8 | get(buf);
  }
}

static void
readliteral_attr(struct buf *b, termchar *c, termline *unused(line))
{
  cattrflags attr;
  uint fg = 0;
  uint bg = 0;
  colour ul = (colour)-1;

  attr = get(b) << 16;
  attr |= get(b) << 8;
  attr |= get(b);
  if (attr >= 0x800000) {
    attr &= ~0x800000;
    attr <<= 8;
    attr |= get(b);
    attr <<= 8;
    attr |= get(b);
    attr <<= 8;
    attr |= get(b);
    attr <<= 8;
    attr |= get(b);
    attr <<= 8;
    attr |= get(b);

    fg = get(b) << 16;
    fg |= get(b) << 8;
    fg |= get(b);
    bg = get(b) << 16;
    bg |= get(b) << 8;
    bg |= get(b);
    ul = get(b) << 16;
    ul |= get(b) << 8;
    ul |= get(b);
  }

  c->attr.attr = attr;
  c->attr.truefg = fg;
  c->attr.truebg = bg;
  c->attr.ulcolr = ul;
}

static void
readliteral_cc(struct buf *b, termchar *c, termline *line)
{
  termchar n;
  int x = c - line->chars;

  c->cc_next = 0;

  while (1) {
    readliteral_chr(b, &n, line);
    if (!n.chr)
      break;
    readliteral_attr(b, &n, line);
    add_cc(line, x, n.chr, n.attr);
  }
}

static void
makerle(struct buf *b, termline *line,
        void (*makeliteral) (struct buf *b, termchar *c))
{
  int hdrpos, hdrsize, n, prevlen, prevpos, thislen, thispos, prev2;
  termchar *c = line->chars;

  n = line->cols;

  hdrpos = b->len;
  hdrsize = 0;
  add(b, 0);
  prevlen = prevpos = 0;
  prev2 = false;

  while (n-- > 0) {
    thispos = b->len;
    makeliteral(b, c++);
    thislen = b->len - thispos;
    if (thislen == prevlen &&
        !memcmp(b->data + prevpos, b->data + thispos, thislen)) {
     /*
      * This literal precisely matches the previous one.
      * Turn it into a run if it's worthwhile.
      *
      * With one-byte literals, it costs us two bytes to
      * encode a run, plus another byte to write the header
      * to resume normal output; so a three-element run is
      * neutral, and anything beyond that is unconditionally
      * worthwhile. With two-byte literals or more, even a
      * 2-run is a win.
      */
      if (thislen > 1 || prev2) {
        int runpos, runlen;

       /*
        * It's worth encoding a run. Start at prevpos,
        * unless hdrsize == 0 in which case we can back up
        * another one and start by overwriting hdrpos.
        */

        hdrsize--;      /* remove the literal at prevpos */
        if (prev2) {
          assert(hdrsize > 0);
          hdrsize--;
          prevpos -= prevlen;   /* and possibly another one */
        }

        if (hdrsize == 0) {
          assert(prevpos == hdrpos + 1);
          runpos = hdrpos;
          b->len = prevpos + prevlen;
        }
        else {
          memmove(b->data + prevpos + 1, b->data + prevpos, prevlen);
          runpos = prevpos;
          b->len = prevpos + prevlen + 1;
         /*
          * Terminate the previous run of ordinary
          * literals.
          */
          assert(hdrsize >= 1 && hdrsize <= 128);
          b->data[hdrpos] = hdrsize - 1;
        }

        runlen = prev2 ? 3 : 2;

        while (n > 0 && runlen < 129) {
          int tmppos, tmplen;
          tmppos = b->len;
          makeliteral(b, c);
          tmplen = b->len - tmppos;
          b->len = tmppos;
          if (tmplen != thislen ||
              memcmp(b->data + runpos + 1, b->data + tmppos, tmplen)) {
            break;      /* run over */
          }
          n--, c++, runlen++;
        }

        assert(runlen >= 2 && runlen <= 129);
        b->data[runpos] = runlen + 0x80 - 2;

        hdrpos = b->len;
        hdrsize = 0;
        add(b, 0);
       /* And ensure this run doesn't interfere with the next. */
        prevlen = prevpos = 0;
        prev2 = false;

        continue;
      }
      else {
       /*
        * Just flag that the previous two literals were
        * identical, in case we find a third identical one
        * we want to turn into a run.
        */
        prev2 = true;
        prevlen = thislen;
        prevpos = thispos;
      }
    }
    else {
      prev2 = false;
      prevlen = thislen;
      prevpos = thispos;
    }

   /*
    * This character isn't (yet) part of a run. Add it to
    * hdrsize.
    */
    hdrsize++;
    if (hdrsize == 128) {
      b->data[hdrpos] = hdrsize - 1;
      hdrpos = b->len;
      hdrsize = 0;
      add(b, 0);
      prevlen = prevpos = 0;
      prev2 = false;
    }
  }

 /* Clean up. */
  if (hdrsize > 0) {
    assert(hdrsize <= 128);
    b->data[hdrpos] = hdrsize - 1;
  }
  else {
    b->len = hdrpos;
  }
}


uchar *
compressline(termline *line)
{
  struct buf buffer = { null, 0, 0 }, *b = &buffer;

 /*
  * First, store the column count, 7 bits at a time, least
  * significant `digit' first, with the high bit set on all but
  * the last.
  */
  {
    int n = line->cols;
    while (n >= 128) {
      add(b, (uchar) ((n & 0x7F) | 0x80));
      n >>= 7;
    }
    add(b, (uchar) (n));
  }

 /*
  * Next store the line attributes; same principle.
  */
  {
    int n = line->lattr;
    while (n >= 128) {
      add(b, (uchar) ((n & 0x7F) | 0x80));
      n >>= 7;
    }
    add(b, (uchar) (n));
  }

 /*
  * Now we store a sequence of separate run-length encoded
  * fragments, each containing exactly as many symbols as there
  * are columns in the line.
  *
  * All of these have a common basic format:
  *
  *  - a byte 00-7F indicates that X+1 literals follow it
  *  - a byte 80-FF indicates that a single literal follows it
  *    and expects to be repeated (X-0x80)+2 times.
  *
  * The format of the `literals' varies between the fragments.
  */
  makerle(b, line, makeliteral_chr);
  makerle(b, line, makeliteral_attr);
  makerle(b, line, makeliteral_cc);

 /*
  * Trim the allocated memory so we don't waste any, and return.
  */
#ifdef debug_compressline
  printf("compress %d chars -> %d bytes\n", line->size, b->len);
#endif
  return renewn(b->data, b->len);
}

static void
readrle(struct buf *b, termline *line,
        void (*readliteral) (struct buf *b, termchar *c, termline *line))
{
  int n = 0;

  while (n < line->cols) {
    int hdr = get(b);

    if (hdr >= 0x80) {
     /* A run. */

      int pos = b->len, count = hdr + 2 - 0x80;
      while (count--) {
        assert(n < line->cols);
        b->len = pos;
        readliteral(b, line->chars + n, line);
        n++;
      }
    }
    else {
     /* Just a sequence of consecutive literals. */

      int count = hdr + 1;
      while (count--) {
        assert(n < line->cols);
        readliteral(b, line->chars + n, line);
        n++;
      }
    }
  }

  assert(n == line->cols);
}

termline *
decompressline(uchar *data, int *bytes_used)
{
  int ncols, byte, shift;
  struct buf buffer, *b = &buffer;
  termline *line;

  b->data = data;
  b->len = 0;

 /*
  * First read in the column count.
  */
  ncols = shift = 0;
  do {
    byte = get(b);
    ncols |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);

 /*
  * Now create the output termline.
  */
  line = new(termline);
  line->chars = newn(termchar, ncols);
  line->cols = line->size = ncols;
  line->temporary = true;
  line->cc_free = 0;

 /*
  * We must set all the cc pointers in line->chars to 0 right
  * now, so that cc diagnostics that verify the integrity of the
  * whole line will make sense while we're in the middle of
  * building it up.
  */
  {
    int i;
    for (i = 0; i < line->cols; i++)
      line->chars[i].cc_next = 0;
  }

 /*
  * Now read in the line attributes.
  */
  line->lattr = shift = 0;
  do {
    byte = get(b);
    line->lattr |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);

 /*
  * Now we read in each of the RLE streams in turn.
  */
  readrle(b, line, readliteral_chr);
  readrle(b, line, readliteral_attr);
  readrle(b, line, readliteral_cc);

 /* Return the number of bytes read, for diagnostic purposes. */
  if (bytes_used)
    *bytes_used = b->len;

  return line;
}

/*
 * Clear a line, throwing away any combining characters.
 */
void
clearline(termline *line)
{
  line->lattr = LATTR_NORM;
  for (int j = 0; j < line->cols; j++)
    line->chars[j] = term.erase_char;
  if (line->size > line->cols) {
    line->size = line->cols;
    line->chars = renewn(line->chars, line->size);
    line->cc_free = 0;
  }
}

/*
 * Make sure the line is at least `cols' columns wide.
 */
void
resizeline(termline *line, int cols)
{
  int oldcols = line->cols;

  if (cols > oldcols) {

   /*
    * Leave the same amount of cc space as there was to begin with.
    */
    line->size += cols - oldcols;
    line->chars = renewn(line->chars, line->size);
    line->cols = cols;

   /*
    * Move the cc section.
    */
    memmove(line->chars + cols, line->chars + oldcols,
            (line->size - cols) * sizeof(termchar));

   /*
    * Adjust the first cc_next pointer in each list. (All the
    * subsequent ones are still valid because they are
    * relative offsets within the cc block.) Also do the same
    * to the head of the cc_free list.
    */
    for (int i = 0; i < oldcols; i++)
      if (line->chars[i].cc_next)
        line->chars[i].cc_next += cols - oldcols;
    if (line->cc_free)
      line->cc_free += cols - oldcols;

   /*
    * And finally fill in the new space with erase chars. (We
    * don't have to worry about cc lists here, because we
    * _know_ the erase char doesn't have one.)
    */
    for (int i = oldcols; i < cols; i++)
      line->chars[i] = basic_erase_char;
  }
}

/*
 * Get the number of lines in the scrollback.
 */
int
sblines(void)
{
  return term.on_alt_screen ^ term.show_other_screen ? 0 : term.sblines;
}

/*
 * Retrieve a line of the screen or of the scrollback, according to
 * whether the y coordinate is non-negative or negative
 * (respectively).
 */
termline *
fetch_line(int y)
{
  termlines *lines = term.show_other_screen ? term.other_lines : term.lines;

  termline *line;
  if (y >= 0) {
    assert(y < term.rows);
    line = lines[y];
  }
  else {
    assert(-y <= term.sblines);
    y += term.sbpos;
    if (y < 0)
      y += term.sblen; // Scrollback has wrapped round
    uchar *cline = term.scrollback[y];
    line = decompressline(cline, null);
    resizeline(line, term.cols);
  }

  assert(line);
  return line;
}

/* Release a screen or scrollback line */
void
release_line(termline *line)
{
  assert(line);
  if (line->temporary)
    freeline(line);
}


/*
 * To prevent having to run the reasonably tricky bidi algorithm
 * too many times, we maintain a cache of the last lineful of data
 * fed to the algorithm on each line of the display.
 */
static int
term_bidi_cache_hit(int line, termchar *lbefore, int width)
{
  int i;

  if (!term.pre_bidi_cache)
    return false;       /* cache doesn't even exist yet! */

  if (line >= term.bidi_cache_size)
    return false;       /* cache doesn't have this many lines */

  if (!term.pre_bidi_cache[line].chars)
    return false;       /* cache doesn't contain _this_ line */

  if (term.pre_bidi_cache[line].width != width)
    return false;       /* line is wrong width */

  for (i = 0; i < width; i++)
    if (!termchars_equal(term.pre_bidi_cache[line].chars + i, lbefore + i))
      return false;     /* line doesn't match cache */

  return true;  /* all termchars matched */
}

static void
term_bidi_cache_store(int line, termchar *lbefore, termchar *lafter,
                      bidi_char *wcTo, int width, int size, int bidisize)
{
  int i;

  if (!term.pre_bidi_cache || term.bidi_cache_size <= line) {
    int j = term.bidi_cache_size;
    term.bidi_cache_size = line + 1;
    term.pre_bidi_cache = renewn(term.pre_bidi_cache, term.bidi_cache_size);
    term.post_bidi_cache = renewn(term.post_bidi_cache, term.bidi_cache_size);
    while (j < term.bidi_cache_size) {
      term.pre_bidi_cache[j].chars = term.post_bidi_cache[j].chars = null;
      term.pre_bidi_cache[j].width = term.post_bidi_cache[j].width = -1;
      term.pre_bidi_cache[j].forward = term.post_bidi_cache[j].forward = null;
      term.pre_bidi_cache[j].backward = term.post_bidi_cache[j].backward = null;
      j++;
    }
  }

  free(term.pre_bidi_cache[line].chars);
  free(term.post_bidi_cache[line].chars);
  free(term.post_bidi_cache[line].forward);
  free(term.post_bidi_cache[line].backward);

  term.pre_bidi_cache[line].width = width;
  term.pre_bidi_cache[line].chars = newn(termchar, size);
  term.post_bidi_cache[line].width = width;
  term.post_bidi_cache[line].chars = newn(termchar, size);
  term.post_bidi_cache[line].forward = newn(int, width);
  term.post_bidi_cache[line].backward = newn(int, width);

  memcpy(term.pre_bidi_cache[line].chars, lbefore, size * sizeof(termchar));
  memcpy(term.post_bidi_cache[line].chars, lafter, size * sizeof(termchar));
  memset(term.post_bidi_cache[line].forward, 0, width * sizeof(int));
  memset(term.post_bidi_cache[line].backward, 0, width * sizeof(int));

  int ib = 0;
  for (i = 0; i < width; i++) {
    while (wcTo[ib].index == -1)
      ib++;

    int p = wcTo[ib].index;

    assert(0 <= p && p < width);

    term.post_bidi_cache[line].backward[i] = p;
    term.post_bidi_cache[line].forward[p] = i;

    ib++;
  }
  (void)bidisize;
  assert(ib == bidisize);
}

#ifdef debug_bidi
void trace_bidi(char * tag, bidi_char * wc)
{
  printf("%s", tag);
  for (int i = 0; i < term.cols; i++)
    if (wc[i].wc != ' ') printf(" %04X", wc[i].wc);
  printf("\n");
}
#else
#define trace_bidi(tag, wc)	
#endif

/*
 * Prepare the bidi information for a screen line. Returns the
 * transformed list of termchars, or null if no transformation at
 * all took place (because bidi is disabled). If return was
 * non-null, auxiliary information such as the forward and reverse
 * mappings of permutation position are available in
 * term.post_bidi_cache[scr_y].*.
 */
termchar *
term_bidi_line(termline *line, int scr_y)
{
  if ((line->lattr & LATTR_NOBIDI) || term.disable_bidi
      || cfg.bidi == 0
      || (cfg.bidi == 1 && (term.on_alt_screen ^ term.show_other_screen))
     )
    return null;

  termchar *lchars;
  int it, ib;

 /* Do Arabic shaping and bidi. */

  if (!term_bidi_cache_hit(scr_y, line->chars, term.cols)) {

    if (term.wcFromTo_size < term.cols) {
      term.wcFromTo_size = term.cols;
      term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
      term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
    }

    ib = 0;
    for (it = 0; it < term.cols; it++) {
      ucschar c = line->chars[it].chr;

      if ((c & 0xFC00) == 0xD800) {
        int off = line->chars[it].cc_next;
        if (off) {
          ucschar low_surrogate = line->chars[it + off].chr;
          if ((low_surrogate & 0xFC00) == 0xDC00) {
            c = ((c - 0xD7C0) << 10) | (low_surrogate & 0x03FF);
          }
        }
      }

      if (it) {
        termchar * bp = &line->chars[it - 1];
        // Unfold directional formatting characters which are handled
        // like combining characters in the mintty structures
        // (and would thus stay hidden from minibidi), and need to be
        // exposed as separate characters for the minibidi algorithm
        while (bp->cc_next) {
          bp += bp->cc_next;
          if (bp->chr == 0x200E || bp->chr == 0x200F
              || (bp->chr >= 0x202A && bp->chr <= 0x202E)
              || (bp->chr >= 0x2066 && bp->chr <= 0x2069)
             )
          {
            term.wcFromTo_size++;
            term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
            term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
            term.wcFrom[ib].origwc = term.wcFrom[ib].wc = bp->chr;
            term.wcFrom[ib].index = -1;
            ib++;
          }
        }
      }

      term.wcFrom[ib].origwc = term.wcFrom[ib].wc = c;
      term.wcFrom[ib].index = it;

      ib++;
    }

    trace_bidi("=", term.wcFrom);
    do_bidi(term.wcFrom, ib);
    trace_bidi(":", term.wcFrom);
    do_shape(term.wcFrom, term.wcTo, ib);
    trace_bidi("~", term.wcTo);

    if (term.ltemp_size < line->size) {
      term.ltemp_size = line->size;
      term.ltemp = renewn(term.ltemp, term.ltemp_size);
    }

    memcpy(term.ltemp, line->chars, line->size * sizeof(termchar));

    ib = 0;
    for (it = 0; it < term.cols; it++) {
      while (term.wcTo[ib].index == -1)
        ib++;

      term.ltemp[it] = line->chars[term.wcTo[ib].index];
      if (term.ltemp[it].cc_next)
        term.ltemp[it].cc_next -= it - term.wcTo[ib].index;

      if (term.wcTo[ib].origwc != term.wcTo[ib].wc)
        term.ltemp[it].chr = term.wcTo[ib].wc;

      ib++;
    }
    term_bidi_cache_store(scr_y, line->chars, term.ltemp, term.wcTo,
                          term.cols, line->size, ib);

    lchars = term.ltemp;
  }
  else {
    lchars = term.post_bidi_cache[scr_y].chars;
  }

  return lchars;
}
