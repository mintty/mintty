#ifndef MINIBIDI_H
#define MINIBIDI_H

typedef unsigned int ucschar;

typedef struct {
  ucschar origwc, wc;
  short index;
  uchar wide;  // width marker for term_bidi_line()
  uchar emojilen: 7;
} bidi_char;

int do_bidi(bool autodir, int para_level, bool explicitRTL, bool box_mirror, bidi_char * line, int count);
int do_shape(bidi_char * line, bidi_char * to, int count);

/* bidi classes (Unicode: PropertyValueAliases.txt) */
enum {
  L, LRE, LRO, R, AL, RLE, RLO, PDF, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON,
  LRI, RLI, FSI, PDI
};

uchar bidi_class(ucschar ch);
bool is_sep_class(uchar bc);
bool is_punct_class(uchar bc);
bool is_rtl_class(uchar bc);

#endif
