// termline.c (part of mintty)
// Copyright 2008-12 Andy Koppe, -2019 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"
#include "win.h"  // cfg.bidi


#define newn_1(poi, type, count)	{poi = newn(type, count + 1); poi++;}
#define renewn_1(poi, count)	{poi--; poi = renewn(poi, count + 1); poi++;}


termline *
newline(int cols, int bce)
{
  termline *line = new(termline);
  newn_1(line->chars, termchar, cols);
  //! Note: line->chars is based @ index -1
  for (int j = -1; j < cols; j++)
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
  //! Note: line->chars is based @ index -1
  free(&line->chars[-1]);
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

// Note: Inlining the add() function reduces the compression time by 30%.
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
 * Store the number, 7 bits at a time, least significant
 * `digit' first, with the high bit set on all but the last.
 */
static void
writevint(struct buf *b, int n)
{
  while (n >= 128) {
    add(b, (uchar) ((n & 0x7F) | 0x80));
    n >>= 7;
  }
  add(b, (uchar) (n));
}

static int
readvint(struct buf *b)
{
  int val = 0;
  int shift = 0;
  int byte = 0;
  do {
    byte = get(b);
    val |= (byte & 0x7F) << shift;
    shift += 7;
  } while (byte & 0x80);
  return val;
}

/*
 * Add a combining character to a character cell.
 */
void
add_cc(termline *line, int col, wchar chr, cattr attr)
{
  assert(col >= -1 && col < line->cols);

 /*
  * Start by extending the cols array if the free list is empty.
  */
  if (!line->cc_free) {
    int n = line->size;
    line->size += 16 + (line->size - line->cols) / 2;
    renewn_1(line->chars, line->size);
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
  * `col' now points at the last cc currently in this cell; 
  * so we simply add another one.
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

  assert(col >= -1 && col < line->cols);

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

// Not using RLE here, because the only compressible thing is the white space.
// The repetitive white space is encoded with 1 space followed by a counter.
static void
encode_chr(struct buf *b, termline *line)
{
  int space_cnt = 0;
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ++i) {
    termchar *c = &line->chars[i];
    if (c->chr == ' ') {
      space_cnt++;
      continue;
    }

    if (space_cnt > 0) {
      add(b, ' ');
      writevint(b, space_cnt);
      space_cnt = 0;
    }
    makeliteral_chr(b, c);
  }

  if (space_cnt > 0) {
    add(b, ' ');
    writevint(b, space_cnt);
  }
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
  int link = c->attr.link;
  int imgi = c->attr.imgi;
  colour truefg = c->attr.truefg;
  colour truebg = c->attr.truebg;
  colour ulcolr = c->attr.ulcolr;

  if (attr < 0x800000 && !truefg && !truebg
      && link == -1 && !imgi && ulcolr == (colour)-1) {
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
    add(b, (uchar) ((link >> 24) & 0xFF));
    add(b, (uchar) ((link >> 16) & 0xFF));
    add(b, (uchar) ((link >> 8) & 0xFF));
    add(b, (uchar) (link & 0xFF));
    add(b, (uchar) ((imgi >> 24) & 0xFF));
    add(b, (uchar) ((imgi >> 16) & 0xFF));
    add(b, (uchar) ((imgi >> 8) & 0xFF));
    add(b, (uchar) (imgi & 0xFF));

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

static bool
cattr_eq(const cattr *a, const cattr *b)
{
  return a->attr == b->attr
    && a->truebg == b->truebg
    && a->truefg == b->truefg
    && a->ulcolr == b->ulcolr
    && a->link == b->link
    && a->imgi == b->imgi;
}

// Using RLE here, each cattr is followed by a counter.
static void
encode_attr(struct buf *b, termline *line)
{
  int head = -1;
  int rle_cnt = 0;
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ++i) {
    termchar *c = &line->chars[i];
    if (cattr_eq(&c->attr, &line->chars[head].attr)) {
      rle_cnt++;
    } else {
      makeliteral_attr(b, &line->chars[head]);
      writevint(b, rle_cnt);
      head = i;
      rle_cnt = 1;
    }
  }
  makeliteral_attr(b, &line->chars[head]);
  writevint(b, rle_cnt);
}

// This is similar to encode_chr(), only repetitive zeros are compressed.
static void
encode_cc(struct buf *b, termline *line)
{
  uint16_t zero_cnt = 0;
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ++i) {
    termchar *c = &line->chars[i];
    if (c->cc_next == 0) {
      zero_cnt++;
      continue;
    }

    if (zero_cnt > 0) {
      writevint(b, 0);
      writevint(b, zero_cnt);
      zero_cnt = 0;
    }
    writevint(b, c->cc_next);
  }

  if (zero_cnt > 0) {
    writevint(b, 0);
    writevint(b, zero_cnt);
  }
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
decode_chr(struct buf *b, termline *line)
{
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ) {
    termchar *c = &line->chars[i];
    readliteral_chr(b, c, line);
    if (c->chr == ' ') {
      int space_cnt = readvint(b);
      for (int j = 1; j < space_cnt; ++j) {
        (c + j)->chr = ' ';
      }
      i += space_cnt;
    } else {
      i++;
    }
  }
}

static void
readliteral_attr(struct buf *b, termchar *c, termline *unused(line))
{
  cattrflags attr;
  int link = -1;
  int imgi = 0;
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
    link = get(b) << 24;
    link |= get(b) << 16;
    link |= get(b) << 8;
    link |= get(b);
    imgi = get(b) << 24;
    imgi |= get(b) << 16;
    imgi |= get(b) << 8;
    imgi |= get(b);

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
  c->attr.link = link;
  c->attr.imgi = imgi;
  c->attr.truefg = fg;
  c->attr.truebg = bg;
  c->attr.ulcolr = ul;
}

static void
decode_attr(struct buf *b, termline *line)
{
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ) {
    termchar *c = &line->chars[i];
    readliteral_attr(b, c, line);
    int rle_cnt = readvint(b);
    i += rle_cnt;
    for (int j = 1; j < rle_cnt; ++j) {
      (c + j)->attr = c->attr;
    }
  }
}

static void
decode_cc(struct buf *b, termline *line)
{
  //! Note: line->chars is based @ index -1
  for (int i = -1; i < line->size; ) {
    termchar *c = &line->chars[i];
    c->cc_next = readvint(b);
    if (c->cc_next == 0) {
      int zero_cnt = readvint(b);
      for (int j = 1; j < zero_cnt; ++j) {
        (c + j)->cc_next = 0;
      }
      i += zero_cnt;
    } else {
      i++;
    }
  }
}

uchar *
compressline(termline *line)
{
  struct buf buffer = { null, 0, 0 }, *b = &buffer;

  // Store the column count, array size, cc_free and line attributes.
  writevint(b, line->cols);
  writevint(b, line->size);
  writevint(b, line->cc_free);
  writevint(b, line->lattr);

 /*
  * Store the wrap position if used.
  */
  if (line->lattr & LATTR_WRAPPED) {
    writevint(b, line->wrappos);
  }

  // FULL-TERMCHAR
  // For each field in termchar, encode them separately.
  // The whole line->chars array (from -1 to line->size) is encoded in each pass,
  // no special handling is needed for the linked list inside line->chars.
  encode_chr(b, line);
  encode_attr(b, line);
  encode_cc(b, line);

 /*
  * Trim the allocated memory so we don't waste any, and return.
  */
#ifdef debug_compressline
  printf("compress %d chars -> %d bytes\n", line->size, b->len);
#endif
  return renewn(b->data, b->len);
}

termline *
decompressline(uchar *data, int *bytes_used)
{
  struct buf buffer, *b = &buffer;
  termline *line;

  b->data = data;
  b->len = 0;

 /*
  * First read in the column count and array size.
  */
  int ncols = readvint(b);
  int size = readvint(b);

 /*
  * Now create the output termline.
  */
  line = new(termline);
  newn_1(line->chars, termchar, size);
  line->cols = ncols;
  line->size = size;
  line->temporary = true;
  // read in the cc_free.
  line->cc_free = (short) readvint(b);

 /*
  * Now read in the line attributes.
  */
  line->lattr = (ushort) readvint(b);

 /*
  * Read the wrap position if used.
  */
  if (line->lattr & LATTR_WRAPPED) {
    line->wrappos = (ushort) readvint(b);
  }

  // FULL-TERMCHAR
  decode_chr(b, line);
  decode_attr(b, line);
  decode_cc(b, line);

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
  //! Note: line->chars is based @ index -1
  for (int j = -1; j < line->cols; j++)
    line->chars[j] = term.erase_char;
  if (line->size > line->cols) {
    line->size = line->cols;
    renewn_1(line->chars, line->size);
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
    renewn_1(line->chars, line->size);
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
    //! Note: line->chars is based @ index -1
    for (int i = -1; i < oldcols; i++)
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
 * whether the y coordinate is non-negative or negative (respectively).
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
term_bidi_cache_hit(int line, termchar *lbefore, ushort lattr, int width)
{
  int i;

  if (!term.pre_bidi_cache)
    return false;       /* cache doesn't even exist yet! */

  if (line >= term.bidi_cache_size)
    return false;       /* cache doesn't have this many lines */

  if (!term.pre_bidi_cache[line].chars)
    return false;       /* cache doesn't contain _this_ line */

  if (term.pre_bidi_cache[line].lattr != (lattr & LATTR_BIDIMASK))
    return false;       /* bidi attributes may be different */

  if (term.pre_bidi_cache[line].width != width)
    return false;       /* line is wrong width */

  for (i = 0; i < width; i++)
    if (!termchars_equal(term.pre_bidi_cache[line].chars + i, lbefore + i))
      return false;     /* line doesn't match cache */

  return true;  /* all termchars matched */
}

static void
term_bidi_cache_store(int line, 
                      termchar *lbefore, termchar *lafter, bidi_char *wcTo, 
                      ushort lattr, int width, int size, int bidisize)
{
#ifdef debug_bidi_cache
  printf("cache_store w %d s %d bs %d\n", width, size, bidisize);
#endif

  int i;

  if (!term.pre_bidi_cache || term.bidi_cache_size <= line) {
    int j = term.bidi_cache_size;
    term.bidi_cache_size = line + 1;
    term.pre_bidi_cache = renewn(term.pre_bidi_cache, term.bidi_cache_size);
    term.post_bidi_cache = renewn(term.post_bidi_cache, term.bidi_cache_size);
    while (j < term.bidi_cache_size) {
      term.pre_bidi_cache[j].chars = term.post_bidi_cache[j].chars = null;
      term.pre_bidi_cache[j].lattr = -1;
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

  term.pre_bidi_cache[line].lattr = lattr & LATTR_BIDIMASK;
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

    if (wcTo[ib].wide && i + 1 < width) {
      // compensate for skipped wide character right half
      i++;
      p++;
      term.post_bidi_cache[line].backward[i] = p;
      term.post_bidi_cache[line].forward[p] = i;
    }

    ib++;
  }
  (void)bidisize;
  assert(ib == bidisize);
}

#define dont_debug_bidi

#ifdef debug_bidi
void trace_bidi(char * tag, bidi_char * wc, int ib)
{
  printf("%s[%d]", tag, ib);
  for (int i = 0; i < ib; i++)
    //if (wc[i].wc != ' ')
      printf(" [2m%2d:[m%02X", wc[i].index, wc[i].wc);
  printf("\n");
}
#else
#define trace_bidi(tag, wc, ib)	
#endif

wchar *
wcsline(termline * line)
{
  static wchar * wcs = 0;
  wcs = renewn(wcs, term.cols + 1);
  for (int i = 0; i < term.cols; i++)
    wcs[i] = line->chars[i].chr;
  wcs[term.cols] = 0;
  return wcs;
}

ushort
getparabidi(termline * line)
{
  ushort parabidi = line->lattr & LATTR_BIDIMASK;
  if (parabidi & (LATTR_BIDISEL | LATTR_AUTOSEL))
    return parabidi;

  // autodetection of line direction (UBA P2 and P3);
  // this needs in fact to be done both when called from 
  // write_char (output phase) and term_bidi_line (display phase)
  bool det = false;
  int isolateLevel = 0;
  int paragraphLevel = !!(parabidi & LATTR_BIDIRTL);
  for (int i = 0; i < line->cols; i++) {
    int type = bidi_class(line->chars[i].chr);
    if (type == LRI || type == RLI || type == FSI)
      isolateLevel++;
    else if (type == PDI)
      isolateLevel--;
    else if (isolateLevel == 0) {
      if (type == R || type == AL) {
        paragraphLevel = 1;
        det = true;
        break;
      }
      else if (type == L) {
        paragraphLevel = 0;
        det = true;
        break;
      }
    }
  }
  if (paragraphLevel & 1)
    parabidi |= LATTR_AUTORTL;
  else
    parabidi &= ~LATTR_AUTORTL;
  if (det)
    parabidi |= LATTR_AUTOSEL;

  return parabidi;
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
term_bidi_line(termline *line, int scr_y)
{
  bool autodir = !(line->lattr & (LATTR_BIDISEL | LATTR_AUTOSEL));
  int level = (line->lattr & LATTR_BIDIRTL) ? 1 : 0;
  bool explicitRTL = (line->lattr & LATTR_NOBIDI) && level == 1;
  //printf("bidilin @%d %04X %.22ls auto %d lvl %d\n", scr_y, line->lattr, wcsline(line), autodir, level);

#define support_multiline_bidi
  //TODO: this multi-line ("paragraph") bidi direction handling seems to work
  // but the implementation is a mess, 
  // also it handles direction autodetection only, not multi-line resolving;
  // this should eventually be replaced by paragraph handling in term_paint

#ifdef support_multiline_bidi
  // within a "paragraph" (in a wrapped continuation line), 
  // consult previous line (if there is one)
  bool prevseldir = false;
  if (autodir && line->lattr & LATTR_WRAPCONTD && scr_y > -sblines()) {
    // look backward to beginning of paragraph or an already determined line,
    // in case previous lines are not displayed (scrolled out)
    int y = scr_y - 1;
    int paray = scr_y;
    ushort parabidi = (ushort)-1;
    bool brk = false;
    do {
      termline *prevline = fetch_line(term.disptop + y);
      //printf("back @%d %04X %.22ls auto %d lvl %d\n", y, prevline->lattr, wcsline(prevline), autodir, level);
      if (prevline->lattr & LATTR_WRAPPED) {
        paray = y;
        if (prevline->lattr & (LATTR_BIDISEL | LATTR_AUTOSEL)) {
          prevseldir = true;
          parabidi = prevline->lattr & LATTR_BIDIMASK;
          brk = true;
        }
        else if (!(prevline->lattr & LATTR_WRAPCONTD))
          brk = true;
      }
      else
        brk = true;
      release_line(prevline);
      if (brk)
        break;
      y--;
    } while (y >= -sblines());

    // if a previously determined direction was found, use it for current line
    if (prevseldir) {
#ifdef propagate_to_intermediate_apparently_useless
      // also propagate it to intermediate lines
      while (paray <= scr_y) {
        termline *prevline = fetch_line(term.disptop + paray);
        prevline->lattr = (prevline->lattr & ~LATTR_BIDIMASK) | parabidi;
        //printf("prop @%d %04X %.22ls auto %d lvl %d\n", paray, prevline->lattr, wcsline(prevline), autodir, level);
        release_line(prevline);
        paray++;
      }
#else
      (void)paray;
#endif
      line->lattr = (line->lattr & ~LATTR_BIDIMASK) | parabidi;
      autodir = !(line->lattr & (LATTR_BIDISEL | LATTR_AUTOSEL));
      level = (line->lattr & LATTR_BIDIRTL) ? 1 : 0;
    }
    //printf("line @%d %04X %.22ls auto %d lvl %d\n", scr_y, line->lattr, wcsline(line), autodir, level);
  }
  //printf("pluq @%d/%d %04X %.22ls auto %d prevsel %d\n", scr_y, term.disptop, line->lattr, wcsline(line), autodir, prevseldir);
  if (autodir && !prevseldir && (line->lattr & LATTR_WRAPPED) && term.disptop + scr_y < 0) {
    // if we are yet unsure about the direction, 
    // and if we are displaying from scrollback buffer, 
    // we may need to inspect the remainder of the paragraph
    termline *succline = line;
    ushort parabidi = getparabidi(line);
    //printf("plus @%d %04X/%04X %.22ls auto %d lvl %d\n", scr_y, succline->lattr, parabidi, wcsline(succline), autodir, level);
    autodir = !(parabidi & (LATTR_BIDISEL | LATTR_AUTOSEL));
    int y = scr_y;
    while (autodir) {
      y++;
      if (y >= term.rows)
        break;

      succline = fetch_line(term.disptop + y);
      ushort lattr = succline->lattr;
      parabidi = getparabidi(succline);
      //printf("plus @%d %04X/%04X %.22ls auto %d lvl %d\n", y, lattr, parabidi, wcsline(succline), autodir, level);
      release_line(succline);

      if (!(lattr & LATTR_WRAPCONTD))
        break;
      autodir = !(parabidi & (LATTR_BIDISEL | LATTR_AUTOSEL));
      if (!autodir) {
        line->lattr = (line->lattr & ~LATTR_BIDIMASK) | parabidi;
        level = (parabidi & LATTR_BIDIRTL) ? 1 : 0;
      }
      if (!(lattr & LATTR_WRAPPED))
        break;
    }
  }
#endif

  // if we have autodetected the direction for this line already,
  // determine its paragraph embedding level
  if (line->lattr & LATTR_AUTOSEL)
    level = (line->lattr & LATTR_AUTORTL) ? 1 : 0;

  // if no bidi handling is required for this line, skip the rest
  if (((line->lattr & LATTR_NOBIDI) && !explicitRTL)
      || term.disable_bidi
      || cfg.bidi == 0
      || (cfg.bidi == 1 && (term.on_alt_screen ^ term.show_other_screen))
     )
    return null;

  termchar *lchars;
  int it, ib;

 /* Do Arabic shaping and bidi. */

  if (!term_bidi_cache_hit(scr_y, line->chars, line->lattr, term.cols)) {

    if (term.wcFromTo_size < term.cols) {
      term.wcFromTo_size = term.cols;
      term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
      term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
    }

    // UTF-16 string (including surrogates) for Windows bidi calculation
    //wchar wcs[2 * term.cols];  /// size handling to be tweaked
    //int wcsi = 0;

    ib = 0;
#ifdef apply_HL3
    uint emojirest = 0;
#endif
    for (it = 0; it < term.cols; it++) {
      ucschar c = line->chars[it].chr;
      //wcs[wcsi++] = c;

      if ((c & 0xFC00) == 0xD800) {
        int off = line->chars[it].cc_next;
        if (off) {
          ucschar low_surrogate = line->chars[it + off].chr;
          if ((low_surrogate & 0xFC00) == 0xDC00) {
            c = ((c - 0xD7C0) << 10) | (low_surrogate & 0x03FF);
            //wcs[wcsi++] = low_surrogate;
          }
        }
      }

      if (!it) {
        // Handle initial combining characters, esp. directional markers
        termchar * bp = &line->chars[-1];
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
            term.wcFrom[ib].wide = false;
            term.wcFrom[ib].emojilen = 0;
            ib++;
            //wcs[wcsi++] = bp->chr;
          }
        }
      }

      // collapse dummy wide second half placeholders
      if (c != UCSWIDE) {
#ifdef apply_HL3
        if (line->chars[it].attr.attr & TATTR_EMOJI) {
          uint emojilen = line->chars[it].attr.attr & ATTR_FGMASK;
          if (emojilen > 1) {
            if (!emojirest) {
              term.wcFromTo_size++;
              term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
              term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
              term.wcFrom[ib].origwc = term.wcFrom[ib].wc = 0x202D; // LRO
              term.wcFrom[ib].index = -1;
              term.wcFrom[ib].wide = false;
              ib++;
            }
            emojirest = emojilen;
          }
        }
#endif

        term.wcFrom[ib].origwc = term.wcFrom[ib].wc = c;
        term.wcFrom[ib].index = it;
        term.wcFrom[ib].wide = false;
        term.wcFrom[ib].emojilen = 
          line->chars[it].attr.attr & TATTR_EMOJI
          ? (line->chars[it].attr.attr & ATTR_FGMASK ?: 1)
          : 0;
        ib++;
      }
      else if (ib) {
        // skip wide character virtual right half, flag it
        term.wcFrom[ib - 1].wide = true;
      }

#ifdef apply_HL3
#warning this does not work properly, may even crash
      if (emojirest) {
        emojirest--;
        if (!emojirest) {
          term.wcFromTo_size++;
          term.wcFrom = renewn(term.wcFrom, term.wcFromTo_size);
          term.wcTo = renewn(term.wcTo, term.wcFromTo_size);
          term.wcFrom[ib].origwc = term.wcFrom[ib].wc = 0x202C; // PDF
          term.wcFrom[ib].index = -1;
          term.wcFrom[ib].wide = false;
          ib++;
        }
      }
#endif

      termchar * bp = &line->chars[it];
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
          term.wcFrom[ib].wide = false;
          term.wcFrom[ib].emojilen = 0;
          ib++;
          //wcs[wcsi++] = bp->chr;
        }
      }
    }

    trace_bidi("=", term.wcFrom, ib);
    int rtl = do_bidi(autodir, level, explicitRTL,
                      line->lattr & LATTR_BOXMIRROR,
                      term.wcFrom, ib);
    trace_bidi(":", term.wcFrom, ib);

#ifdef support_multiline_bidi
    if (autodir && rtl >= 0) {
      line->lattr |= LATTR_AUTOSEL;
      if (rtl & 1)
        line->lattr |= LATTR_AUTORTL;
      else
        line->lattr &= ~LATTR_AUTORTL;
      if (true) {  // limiting to prevseldir does not work
        ushort parabidi = line->lattr & LATTR_BIDIMASK;
        //printf("bidi @%d %04X %.22ls rtl %d auto %d lvl %d\n", scr_y, line->lattr, wcsline(line), rtl, autodir, level);
        termline * paraline = line;
        int paray = scr_y;
        while ((paraline->lattr & LATTR_WRAPCONTD) && paray > -sblines()) {
          paraline = fetch_line(--paray);
          bool brk = false;
          if (paraline->lattr & LATTR_WRAPPED) {
            paraline->lattr = (paraline->lattr & ~LATTR_BIDIMASK) | parabidi;
            //printf("post @%d %04X %.22ls auto %d lvl %d\n", paray, paraline->lattr, wcsline(paraline), autodir, level);
#ifdef use_invalidate_useless
            if (paray >= 0)
              term_invalidate(0, paray, term.cols, paray);
#endif
          }
          else
            brk = true;
          release_line(paraline);
          if (brk)
            break;
        }
      }
    }
#else
    (void)rtl;
#endif

#ifdef refresh_parabidi_after_bidi
//? if at all, this is only useful after modification of wrapped lines
    if (line->lattr & LATTR_WRAPPED && scr_y + 1 < term.rows) {
      int conty = scr_y + 1;
      do {
        termline *contline = term.lines[conty];
        if (!(contline->lattr & LATTR_WRAPCONTD))
          break;
        if (rtl)
          contline->lattr |= (LATTR_AUTORTL | LATTR_AUTOSEL);
        else {
          contline->lattr &= ~LATTR_AUTORTL;
          contline->lattr |= LATTR_AUTOSEL;
        }
        /// if changed, invalidate display line
        /// also propagate back to beginning of paragraph

        if (!(contline->lattr & LATTR_WRAPPED))
          break;
        conty++;
      } while (conty < term.rows);
    }
#endif

    do_shape(term.wcFrom, term.wcTo, ib);
    trace_bidi("~", term.wcTo, ib);

    if (term.ltemp_size < line->size) {
      term.ltemp_size = line->size;
      term.ltemp = renewn(term.ltemp, term.ltemp_size);
    }

    // copy line->chars to ltemp initially, esp. to preserve all combinings
    memcpy(term.ltemp, line->chars, line->size * sizeof(termchar));

    // equip ltemp with reorder line->chars as determined in wcTo
    ib = 0;
    for (it = 0; it < term.cols; it++) {
      while (term.wcTo[ib].index == -1)
        ib++;

      // copy character and combining reference from source as reordered
      //printf("reorder %2d <- [%2d]%2d: %5X %d\n", it, ib, term.wcTo[ib].index, term.wcTo[ib].wc, term.wcTo[ib].wide);
      term.ltemp[it] = line->chars[term.wcTo[ib].index];
      if (term.ltemp[it].cc_next)
        term.ltemp[it].cc_next -= it - term.wcTo[ib].index;

      // update reshaped glyphs
      if (term.wcTo[ib].origwc != term.wcTo[ib].wc)
        term.ltemp[it].chr = term.wcTo[ib].wc;

      // expand wide characters to their double-half representation
      if (term.wcTo[ib].wide && it + 1 < term.cols && term.wcTo[ib].index + 1 < term.cols) {
        term.ltemp[++it] = line->chars[term.wcTo[ib].index + 1];
      }

      ib++;
    }
    term_bidi_cache_store(scr_y, line->chars, term.ltemp, term.wcTo,
                          line->lattr, term.cols, line->size, ib);
#ifdef debug_bidi_cache
    for (int i = 0; i < term.cols; i++)
      printf(" %04X", term.ltemp[i].chr);
    printf("\n");
    for (int i = 0; i < ib; i++)
      printf(" %04X[%d]", term.wcTo[i].wc, term.wcTo[i].index);
    printf("\n");
    for (int i = 0; i < term.cols; i++)
      printf(" %d", term.post_bidi_cache[scr_y].forward[i]);
    printf("\n");
    for (int i = 0; i < term.cols; i++)
      printf(" %d", term.post_bidi_cache[scr_y].backward[i]);
    printf("\n");
#endif

    lchars = term.ltemp;
  }
  else {
    lchars = term.post_bidi_cache[scr_y].chars;
  }

  return lchars;
}
