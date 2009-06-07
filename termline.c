// termline.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

termline *
newline(int cols, int bce)
{
  termline *line;
  int j;

  line = new(termline);
  line->chars = newn(termchar, cols);
  for (j = 0; j < cols; j++)
    line->chars[j] = (bce ? term.erase_char : term.basic_erase_char);
  line->cols = line->size = cols;
  line->lattr = LATTR_NORM;
  line->temporary = false;
  line->cc_free = 0;

  return line;
}

void
freeline(termline * line)
{
  if (line) {
    free(line->chars);
    free(line);
  }
}

void
unlineptr(termline * line)
{
  if (line->temporary)
    freeline(line);
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
add_cc(termline * line, int col, uint chr)
{
  int newcc;

  assert(col >= 0 && col < line->cols);

 /*
  * Start by extending the cols array if the free list is empty.
  */
  if (!line->cc_free) {
    int n = line->size;
    line->size += 16 + (line->size - line->cols) / 2;
    line->chars = renewn(line->chars, line->size);
    line->cc_free = n;
    while (n < line->size) {
      if (n + 1 < line->size)
        line->chars[n].cc_next = 1;
      else
        line->chars[n].cc_next = 0;
      n++;
    }
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
  newcc = line->cc_free;
  if (line->chars[newcc].cc_next)
    line->cc_free = newcc + line->chars[newcc].cc_next;
  else
    line->cc_free = 0;
  line->chars[newcc].cc_next = 0;
  line->chars[newcc].chr = chr;
  line->chars[col].cc_next = newcc - col;
}

/*
 * Clear the combining character list in a character cell.
 */
void
clear_cc(termline * line, int col)
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
 * Compare two character cells for equality. Special case required
 * in do_paint() where we override what we expect the chr and attr
 * fields to be.
 */
int
termchars_equal_override(termchar * a, termchar * b, uint bchr, uint battr)
{
 /* FULL-TERMCHAR */
  if (a->chr != bchr)
    return false;
  if ((a->attr & ~DATTR_MASK) != (battr & ~DATTR_MASK))
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
termchars_equal(termchar * a, termchar * b)
{
  return termchars_equal_override(a, b, b->chr, b->attr);
}

/*
 * Copy a character cell. (Requires a pointer to the destination
 * termline, so as to access its free list.)
 */
void
copy_termchar(termline * destline, int x, termchar * src)
{
  clear_cc(destline, x);

  destline->chars[x] = *src;    /* copy everything except cc-list */
  destline->chars[x].cc_next = 0;       /* and make sure this is zero */

  while (src->cc_next) {
    src += src->cc_next;
    add_cc(destline, x, src->chr);
  }
}

/*
 * Move a character cell within its termline.
 */
void
move_termchar(termline * line, termchar * dest, termchar * src)
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
makeliteral_chr(struct buf *b, termchar * c, uint * state)
{
 /*
  * My encoding for characters is UTF-8-like, in that it stores
  * 7-bit ASCII in one byte and uses high-bit-set bytes as
  * introducers to indicate a longer sequence. However, it's
  * unlike UTF-8 in that it doesn't need to be able to
  * resynchronise, and therefore I don't want to waste two bits
  * per byte on having recognisable continuation characters.
  * Also I don't want to rule out the possibility that I may one
  * day use values 0x80000000-0xFFFFFFFF for interesting
  * purposes, so unlike UTF-8 I need a full 32-bit range.
  * Accordingly, here is my encoding:
  * 
  * 00000000-0000007F: 0xxxxxxx (but see below)
  * 00000080-00003FFF: 10xxxxxx xxxxxxxx
  * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
  * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
  * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
  * 
  * (`Z' is like `x' but is always going to be zero since the
  * values I'm encoding don't go above 2^32. In principle the
  * five-byte form of the encoding could extend to 2^35, and
  * there could be six-, seven-, eight- and nine-byte forms as
  * well to allow up to 64-bit values to be encoded. But that's
  * completely unnecessary for these purposes!)
  * 
  * The encoding as written above would be very simple, except
  * that 7-bit ASCII can occur in several different ways in the
  * terminal data; sometimes it crops up in the D800 page
  * (CSET_ASCII) but at other times it's in the 0000 page (real
  * Unicode). Therefore, this encoding is actually _stateful_:
  * the one-byte encoding of 00-7F actually indicates `reuse the
  * upper three bytes of the last character', and to encode an
  * absolute value of 00-7F you need to use the two-byte form
  * instead.
  */
  if ((c->chr & ~0x7F) == *state) {
    add(b, (uchar) (c->chr & 0x7F));
  }
  else if (c->chr < 0x4000) {
    add(b, (uchar) (((c->chr >> 8) & 0x3F) | 0x80));
    add(b, (uchar) (c->chr & 0xFF));
  }
  else if (c->chr < 0x200000) {
    add(b, (uchar) (((c->chr >> 16) & 0x1F) | 0xC0));
    add(b, (uchar) ((c->chr >> 8) & 0xFF));
    add(b, (uchar) (c->chr & 0xFF));
  }
  else if (c->chr < 0x10000000) {
    add(b, (uchar) (((c->chr >> 24) & 0x0F) | 0xE0));
    add(b, (uchar) ((c->chr >> 16) & 0xFF));
    add(b, (uchar) ((c->chr >> 8) & 0xFF));
    add(b, (uchar) (c->chr & 0xFF));
  }
  else {
    add(b, 0xF0);
    add(b, (uchar) ((c->chr >> 24) & 0xFF));
    add(b, (uchar) ((c->chr >> 16) & 0xFF));
    add(b, (uchar) ((c->chr >> 8) & 0xFF));
    add(b, (uchar) (c->chr & 0xFF));
  }
  *state = c->chr & ~0xFF;
}

static void
makeliteral_attr(struct buf *b, termchar * c, uint *unused(state))
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
  uint attr, colourbits;

  attr = c->attr;

  assert(ATTR_BGSHIFT > ATTR_FGSHIFT);

  colourbits = (attr >> (ATTR_BGSHIFT + 4)) & 0xF;
  colourbits <<= 4;
  colourbits |= (attr >> (ATTR_FGSHIFT + 4)) & 0xF;

  attr =
    (((attr >> (ATTR_BGSHIFT + 8)) << (ATTR_BGSHIFT + 4)) |
     (attr & ((1 << (ATTR_BGSHIFT + 4)) - 1)));
  attr =
    (((attr >> (ATTR_FGSHIFT + 8)) << (ATTR_FGSHIFT + 4)) |
     (attr & ((1 << (ATTR_FGSHIFT + 4)) - 1)));

  attr |= (colourbits << (32 - 9));

  if (attr < 0x8000) {
    add(b, (uchar) ((attr >> 8) & 0xFF));
    add(b, (uchar) (attr & 0xFF));
  }
  else {
    add(b, (uchar) (((attr >> 24) & 0x7F) | 0x80));
    add(b, (uchar) ((attr >> 16) & 0xFF));
    add(b, (uchar) ((attr >> 8) & 0xFF));
    add(b, (uchar) (attr & 0xFF));
  }
}
static void
makeliteral_cc(struct buf *b, termchar * c, uint * unused(state))
{
 /*
  * For combining characters, I just encode a bunch of ordinary
  * chars using makeliteral_chr, and terminate with a \0
  * character (which I know won't come up as a combining char
  * itself).
  * 
  * I don't use the stateful encoding in makeliteral_chr.
  */
  uint zstate;
  termchar z;

  while (c->cc_next) {
    c += c->cc_next;

    assert(c->chr != 0);

    zstate = 0;
    makeliteral_chr(b, c, &zstate);
  }

  z.chr = 0;
  zstate = 0;
  makeliteral_chr(b, &z, &zstate);
}

static void
readliteral_chr(struct buf *b, termchar * c, termline * unused(ldata),
                uint * state)
{
  int byte;

 /*
  * 00000000-0000007F: 0xxxxxxx
  * 00000080-00003FFF: 10xxxxxx xxxxxxxx
  * 00004000-001FFFFF: 110xxxxx xxxxxxxx xxxxxxxx
  * 00200000-0FFFFFFF: 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
  * 10000000-FFFFFFFF: 11110ZZZ xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
  */

  byte = get(b);
  if (byte < 0x80) {
    c->chr = byte | *state;
  }
  else if (byte < 0xC0) {
    c->chr = (byte & ~0xC0) << 8;
    c->chr |= get(b);
  }
  else if (byte < 0xE0) {
    c->chr = (byte & ~0xE0) << 16;
    c->chr |= get(b) << 8;
    c->chr |= get(b);
  }
  else if (byte < 0xF0) {
    c->chr = (byte & ~0xF0) << 24;
    c->chr |= get(b) << 16;
    c->chr |= get(b) << 8;
    c->chr |= get(b);
  }
  else {
    assert(byte == 0xF0);
    c->chr = get(b) << 24;
    c->chr |= get(b) << 16;
    c->chr |= get(b) << 8;
    c->chr |= get(b);
  }
  *state = c->chr & ~0xFF;
}

static void
readliteral_attr(struct buf *b, termchar * c, termline * unused(ldata),
                 uint * unused(state))
{
  uint val, attr, colourbits;

  val = get(b) << 8;
  val |= get(b);

  if (val >= 0x8000) {
    val &= ~0x8000;
    val <<= 16;
    val |= get(b) << 8;
    val |= get(b);
  }

  colourbits = (val >> (32 - 9)) & 0xFF;
  attr = (val & ((1 << (32 - 9)) - 1));

  attr =
    (((attr >> (ATTR_FGSHIFT + 4)) << (ATTR_FGSHIFT + 8)) |
     (attr & ((1 << (ATTR_FGSHIFT + 4)) - 1)));
  attr =
    (((attr >> (ATTR_BGSHIFT + 4)) << (ATTR_BGSHIFT + 8)) |
     (attr & ((1 << (ATTR_BGSHIFT + 4)) - 1)));

  attr |= (colourbits >> 4) << (ATTR_BGSHIFT + 4);
  attr |= (colourbits & 0xF) << (ATTR_FGSHIFT + 4);

  c->attr = attr;
}

static void
readliteral_cc(struct buf *b, termchar * c, termline * ldata,
               uint * unused(state))
{
  termchar n;
  uint zstate;
  int x = c - ldata->chars;

  c->cc_next = 0;

  while (1) {
    zstate = 0;
    readliteral_chr(b, &n, ldata, &zstate);
    if (!n.chr)
      break;
    add_cc(ldata, x, n.chr);
  }
}

static void
makerle(struct buf *b, termline * ldata,
        void (*makeliteral) (struct buf * b, termchar * c, uint * state))
{
  int hdrpos, hdrsize, n, prevlen, prevpos, thislen, thispos, prev2;
  termchar *c = ldata->chars;
  uint state = 0, oldstate;

  n = ldata->cols;

  hdrpos = b->len;
  hdrsize = 0;
  add(b, 0);
  prevlen = prevpos = 0;
  prev2 = false;

  while (n-- > 0) {
    thispos = b->len;
    makeliteral(b, c++, &state);
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
        * unless hdrsize==0 in which case we can back up
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
          oldstate = state;
          makeliteral(b, c, &state);
          tmplen = b->len - tmppos;
          b->len = tmppos;
          if (tmplen != thislen ||
              memcmp(b->data + runpos + 1, b->data + tmppos, tmplen)) {
            state = oldstate;
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
compressline(termline * ldata)
{
  struct buf buffer = { null, 0, 0 }, *b = &buffer;

 /*
  * First, store the column count, 7 bits at a time, least
  * significant `digit' first, with the high bit set on all but
  * the last.
  */
  {
    int n = ldata->cols;
    while (n >= 128) {
      add(b, (uchar) ((n & 0x7F) | 0x80));
      n >>= 7;
    }
    add(b, (uchar) (n));
  }

 /*
  * Next store the lattrs; same principle.
  */
  {
    int n = ldata->lattr;
    while (n >= 128) {
      add(b, (uchar) ((n & 0x7F) | 0x80));
      n >>= 7;
    }
    add(b, (uchar) (n));
  }

 /*
  * Now we store a sequence of separate run-length encoded
  * fragments, each containing exactly as many symbols as there
  * are columns in the ldata.
  * 
  * All of these have a common basic format:
  * 
  *  - a byte 00-7F indicates that X+1 literals follow it
  *  - a byte 80-FF indicates that a single literal follows it
  *    and expects to be repeated (X-0x80)+2 times.
  * 
  * The format of the `literals' varies between the fragments.
  */
  makerle(b, ldata, makeliteral_chr);
  makerle(b, ldata, makeliteral_attr);
  makerle(b, ldata, makeliteral_cc);

 /*
  * Trim the allocated memory so we don't waste any, and return.
  */
  return renewn(b->data, b->len);
}

static void
readrle(struct buf *b, termline * ldata,
        void (*readliteral) (struct buf * b, termchar * c, termline * ldata,
                             uint * state))
{
  int n = 0;
  uint state = 0;

  while (n < ldata->cols) {
    int hdr = get(b);

    if (hdr >= 0x80) {
     /* A run. */

      int pos = b->len, count = hdr + 2 - 0x80;
      while (count--) {
        assert(n < ldata->cols);
        b->len = pos;
        readliteral(b, ldata->chars + n, ldata, &state);
        n++;
      }
    }
    else {
     /* Just a sequence of consecutive literals. */

      int count = hdr + 1;
      while (count--) {
        assert(n < ldata->cols);
        readliteral(b, ldata->chars + n, ldata, &state);
        n++;
      }
    }
  }

  assert(n == ldata->cols);
}

termline *
decompressline(uchar * data, int *bytes_used)
{
  int ncols, byte, shift;
  struct buf buffer, *b = &buffer;
  termline *ldata;

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
  ldata = new(termline);
  ldata->chars = newn(termchar, ncols);
  ldata->cols = ldata->size = ncols;
  ldata->temporary = true;
  ldata->cc_free = 0;

 /*
  * We must set all the cc pointers in ldata->chars to 0 right
  * now, so that cc diagnostics that verify the integrity of the
  * whole line will make sense while we're in the middle of
  * building it up.
  */
  {
    int i;
    for (i = 0; i < ldata->cols; i++)
      ldata->chars[i].cc_next = 0;
  }

 /*
  * Now read in the lattr.
  */
  ldata->lattr = shift = 0;
  do {
    byte = get(b);
    ldata->lattr |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);

 /*
  * Now we read in each of the RLE streams in turn.
  */
  readrle(b, ldata, readliteral_chr);
  readrle(b, ldata, readliteral_attr);
  readrle(b, ldata, readliteral_cc);

 /* Return the number of bytes read, for diagnostic purposes. */
  if (bytes_used)
    *bytes_used = b->len;

  return ldata;
}

/*
 * Resize a line to make it `cols' columns wide.
 */
void
resizeline(termline * line, int cols)
{
  int i, oldcols;

  if (line->cols != cols) {

    oldcols = line->cols;

   /*
    * This line is the wrong length, which probably means it
    * hasn't been accessed since a resize. Resize it now.
    * 
    * First, go through all the characters that will be thrown
    * out in the resize (if we're shrinking the line) and
    * return their cc lists to the cc free list.
    */
    for (i = cols; i < oldcols; i++)
      clear_cc(line, i);

   /*
    * If we're shrinking the line, we now bodily move the
    * entire cc section from where it started to where it now
    * needs to be. (We have to do this before the resize, so
    * that the data we're copying is still there. However, if
    * we're expanding, we have to wait until _after_ the
    * resize so that the space we're copying into is there.)
    */
    if (cols < oldcols)
      memmove(line->chars + cols, line->chars + oldcols,
              (line->size - line->cols) * TSIZE);

   /*
    * Now do the actual resize, leaving the _same_ amount of
    * cc space as there was to begin with.
    */
    line->size += cols - oldcols;
    line->chars = renewn(line->chars, line->size);
    line->cols = cols;

   /*
    * If we're expanding the line, _now_ we move the cc
    * section.
    */
    if (cols > oldcols)
      memmove(line->chars + cols, line->chars + oldcols,
              (line->size - line->cols) * TSIZE);

   /*
    * Go through what's left of the original line, and adjust
    * the first cc_next pointer in each list. (All the
    * subsequent ones are still valid because they are
    * relative offsets within the cc block.) Also do the same
    * to the head of the cc_free list.
    */
    for (i = 0; i < oldcols && i < cols; i++)
      if (line->chars[i].cc_next)
        line->chars[i].cc_next += cols - oldcols;
    if (line->cc_free)
      line->cc_free += cols - oldcols;

   /*
    * And finally fill in the new space with erase chars. (We
    * don't have to worry about cc lists here, because we
    * _know_ the erase char doesn't have one.)
    */
    for (i = oldcols; i < cols; i++)
      line->chars[i] = term.basic_erase_char;
  }
}

/*
 * Get the number of lines in the scrollback.
 */
int
sblines(void)
{
  return term.which_screen == 0 ? count234(term.scrollback) : 0;
}

/*
 * Retrieve a line of the screen or of the scrollback, according to
 * whether the y coordinate is non-negative or negative
 * (respectively).
 */
termline *
(lineptr)(int y)
{
  termline *line;
  tree234 *whichtree;
  int treeindex;

  if (y >= 0) {
    whichtree = term.screen;
    treeindex = y;
  }
  else {
    whichtree = term.scrollback;
    treeindex = y + count234(term.scrollback);
  }
  if (whichtree == term.scrollback) {
    uchar *cline = index234(whichtree, treeindex);
    line = decompressline(cline, null);
  }
  else {
    line = index234(whichtree, treeindex);
  }

  assert(line != null);

  resizeline(line, term.cols);
 /* FIXME: should we sort the compressed scrollback out here? */

  return line;
}


/*
 * To prevent having to run the reasonably tricky bidi algorithm
 * too many times, we maintain a cache of the last lineful of data
 * fed to the algorithm on each line of the display.
 */
static int
term_bidi_cache_hit(int line, termchar * lbefore, int width)
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

  return true;  /* it didn't match. */
}

static void
term_bidi_cache_store(int line, termchar * lbefore, termchar * lafter,
                      bidi_char * wcTo, int width, int size)
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

  memcpy(term.pre_bidi_cache[line].chars, lbefore, size * TSIZE);
  memcpy(term.post_bidi_cache[line].chars, lafter, size * TSIZE);
  memset(term.post_bidi_cache[line].forward, 0, width * sizeof (int));
  memset(term.post_bidi_cache[line].backward, 0, width * sizeof (int));

  for (i = 0; i < width; i++) {
    int p = wcTo[i].index;

    assert(0 <= p && p < width);

    term.post_bidi_cache[line].backward[i] = p;
    term.post_bidi_cache[line].forward[p] = i;
  }
}

/*
 * Prepare the bidi information for a screen line. Returns the
 * transformed list of termchars, or null if no transformation at
 * all took place (because bidi is disabled). If return was
 * non-null, auxiliary information such as the forward and reverse
 * mappings of permutation position are available in
 * term.post_bidi_cache[scr_y].*.
 */
termchar *
term_bidi_line(termline * ldata, int scr_y)
{
  termchar *lchars;
  int it;

 /* Do Arabic shaping and bidi. */

  if (!term_bidi_cache_hit(scr_y, ldata->chars, term.cols)) {

    if (term.wcFromTo_size < term.cols) {
      term.wcFromTo_size = term.cols;
      term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
      term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
    }

    for (it = 0; it < term.cols; it++) {
      uint uc = (ldata->chars[it].chr);

      switch (uc & CSET_MASK) {
        when CSET_LINEDRW: uc = ucsdata.unitab_xterm[uc & 0xFF];
        when CSET_ASCII:   uc = ucsdata.unitab_line[uc & 0xFF];
        when CSET_SCOACS:  uc = ucsdata.unitab_scoacs[uc & 0xFF];
      }
      switch (uc & CSET_MASK) {
        when CSET_ACP:   uc = ucsdata.unitab_font[uc & 0xFF];
        when CSET_OEMCP: uc = ucsdata.unitab_oemcp[uc & 0xFF];
      }

      term.wcFrom[it].origwc = term.wcFrom[it].wc = (wchar) uc;
      term.wcFrom[it].index = it;
    }

    do_bidi(term.wcFrom, term.cols);
    do_shape(term.wcFrom, term.wcTo, term.cols);

    if (term.ltemp_size < ldata->size) {
      term.ltemp_size = ldata->size;
      term.ltemp = renewn(term.ltemp, term.ltemp_size);
    }

    memcpy(term.ltemp, ldata->chars, ldata->size * TSIZE);

    for (it = 0; it < term.cols; it++) {
      term.ltemp[it] = ldata->chars[term.wcTo[it].index];
      if (term.ltemp[it].cc_next)
        term.ltemp[it].cc_next -= it - term.wcTo[it].index;

      if (term.wcTo[it].origwc != term.wcTo[it].wc)
        term.ltemp[it].chr = term.wcTo[it].wc;
    }
    term_bidi_cache_store(scr_y, ldata->chars, term.ltemp, term.wcTo,
                          term.cols, ldata->size);

    lchars = term.ltemp;
  }
  else {
    lchars = term.post_bidi_cache[scr_y].chars;
  }

  return lchars;
}
