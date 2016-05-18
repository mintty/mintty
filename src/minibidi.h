#ifndef MINIBIDI_H
#define MINIBIDI_H

typedef struct {
  wchar origwc, wc;
  ushort index;
} bidi_char;

int do_bidi(bidi_char * line, int count);
int do_shape(bidi_char * line, bidi_char * to, int count);

uchar bidi_class(wchar ch);
bool is_sep_class(uchar bc);
bool is_rtl_class(uchar bc);
bool is_rtl(wchar c);

#endif
