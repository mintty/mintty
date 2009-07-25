#ifndef UNICODE_H
#define UNICODE_H

/* Like Linux use the F000 page for direct to font. */
#define CS_OEMCP   0x0000F000UL       /* OEM Codepage DTF */
#define CS_ACP     0x0000F100UL       /* Ansi Codepage DTF */

/* These are internal use overlapping with the UTF-16 surrogates */
#define CS_ASCII   0x0000D800UL       /* normal ASCII charset ESC ( B */
#define CS_LINEDRW 0x0000D900UL       /* line drawing charset ESC ( 0 */
#define CS_GBCHR   0x0000DB00UL       /* UK variant   charset ESC ( A */
#define CS_MASK    0xFFFFFF00UL       /* Character set mask */

#define UCSERR 0x2592 /* UCS Format error character. */

/*
 * UCSWIDE is a special value used in the terminal data to signify
 * the character cell containing the right-hand half of a CJK wide
 * character.
 */
#define UCSWIDE 0xF000

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
