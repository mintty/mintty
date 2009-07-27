#ifndef UNICODE_H
#define UNICODE_H

#define UCSERR 0x2592 /* UCS Format error character. */

/*
 * UCSWIDE is a special value used in the terminal data to signify
 * the character cell containing the right-hand half of a CJK wide
 * character.
 */
#define UCSWIDE 0

typedef struct {
  uint codepage;
} unicode_data;

extern unicode_data ucsdata;

void init_ucs(void);
int is_dbcs_leadbyte(int codepage, char byte);
int mb_to_wc(int codepage, int flags, const char *mbstr, int mblen,
                                      wchar *wcstr, int wclen);
int wc_to_mb(int codepage, int flags, const wchar * wcstr, int wclen,
                                      char *mbstr, int mblen);
int decode_codepage(char *cp_name);
const char *cp_enumerate(int index);
const char *cp_name(int codepage);
void get_unitab(int codepage, wchar * unitab, int ftype);

int wordtype(int uc);

int wcwidth(wchar);

#endif
