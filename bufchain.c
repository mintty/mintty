#include "bufchain.h"

/* ----------------------------------------------------------------------
 * Generic routines to deal with send buffers: a linked list of
 * smallish blocks, with the operations
 * 
 *  - add an arbitrary amount of data to the end of the list
 *  - remove the first N bytes from the list
 *  - return a (pointer,length) pair giving some initial data in
 *    the list, suitable for passing to a send or write system
 *    call
 *  - retrieve a larger amount of initial data from the list
 *  - return the current size of the buffer chain in bytes
 */

#define BUFFER_GRANULE  512

typedef struct bufchain_link bufchain_link;
struct bufchain_link {
  bufchain_link *next;
  int buflen, bufpos;
  char buf[BUFFER_GRANULE];
};

struct bufchain {
  bufchain_link *head, *tail;
  int buffersize;       /* current amount of buffered data */
};

bufchain *
new_bufchain(void)
{
  bufchain *ch = new(bufchain);
  ch->head = ch->tail = null;
  ch->buffersize = 0;
  return ch;
}

void
bufchain_clear(bufchain * ch)
{
  bufchain_link *b;
  while (ch->head) {
    b = ch->head;
    ch->head = ch->head->next;
    free(b);
  }
  ch->tail = null;
  ch->buffersize = 0;
}

int
bufchain_size(bufchain * ch)
{
  return ch->buffersize;
}

void
bufchain_add(bufchain * ch, const void *data, int len)
{
  const char *buf = (const char *) data;

  if (len == 0)
    return;

  ch->buffersize += len;

  if (ch->tail && ch->tail->buflen < BUFFER_GRANULE) {
    int copylen = min(len, BUFFER_GRANULE - ch->tail->buflen);
    memcpy(ch->tail->buf + ch->tail->buflen, buf, copylen);
    buf += copylen;
    len -= copylen;
    ch->tail->buflen += copylen;
  }
  while (len > 0) {
    int grainlen = min(len, BUFFER_GRANULE);
    bufchain_link *newbuf;
    newbuf = new(bufchain_link);
    newbuf->bufpos = 0;
    newbuf->buflen = grainlen;
    memcpy(newbuf->buf, buf, grainlen);
    buf += grainlen;
    len -= grainlen;
    if (ch->tail)
      ch->tail->next = newbuf;
    else
      ch->head = ch->tail = newbuf;
    newbuf->next = null;
    ch->tail = newbuf;
  }
}

void
bufchain_consume(bufchain * ch, int len)
{
  bufchain_link *tmp;

  assert(ch->buffersize >= len);
  while (len > 0) {
    int remlen = len;
    assert(ch->head != null);
    if (remlen >= ch->head->buflen - ch->head->bufpos) {
      remlen = ch->head->buflen - ch->head->bufpos;
      tmp = ch->head;
      ch->head = tmp->next;
      free(tmp);
      if (!ch->head)
        ch->tail = null;
    }
    else
      ch->head->bufpos += remlen;
    ch->buffersize -= remlen;
    len -= remlen;
  }
}

void
bufchain_prefix(bufchain * ch, void **data, int *len)
{
  *len = ch->head->buflen - ch->head->bufpos;
  *data = ch->head->buf + ch->head->bufpos;
}

void
bufchain_fetch(bufchain * ch, void *data, int len)
{
  bufchain_link *tmp;
  char *data_c = (char *) data;

  tmp = ch->head;

  assert(ch->buffersize >= len);
  while (len > 0) {
    int remlen = len;

    assert(tmp != null);
    if (remlen >= tmp->buflen - tmp->bufpos)
      remlen = tmp->buflen - tmp->bufpos;
    memcpy(data_c, tmp->buf + tmp->bufpos, remlen);

    tmp = tmp->next;
    len -= remlen;
    data_c += remlen;
  }
}
