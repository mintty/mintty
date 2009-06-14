#ifndef UNICODE_H
#define UNICODE_H

/* Like Linux use the F000 page for direct to font. */
#define CSET_OEMCP   0x0000F000UL       /* OEM Codepage DTF */
#define CSET_ACP     0x0000F100UL       /* Ansi Codepage DTF */

/* These are internal use overlapping with the UTF-16 surrogates */
#define CSET_ASCII   0x0000D800UL       /* normal ASCII charset ESC ( B */
#define CSET_LINEDRW 0x0000D900UL       /* line drawing charset ESC ( 0 */
#define CSET_SCOACS  0x0000DA00UL       /* SCO Alternate charset */
#define CSET_GBCHR   0x0000DB00UL       /* UK variant   charset ESC ( A */
#define CSET_MASK    0xFFFFFF00UL       /* Character set mask */

#define DIRECT_CHAR(c) ((c&0xFFFFFC00)==0xD800)
#define DIRECT_FONT(c) ((c&0xFFFFFE00)==0xF000)

#define UCSERR	     (CSET_LINEDRW|'a') /* UCS Format error character. */
/*
 * UCSWIDE is a special value used in the terminal data to signify
 * the character cell containing the right-hand half of a CJK wide
 * character. We use 0xDFFF because it's part of the surrogate
 * range and hence won't be used for anything else (it's impossible
 * to input it via UTF-8 because our UTF-8 decoder correctly
 * rejects surrogates).
 */
#define UCSWIDE	     0xDFFF

typedef struct {
  int dbcs_screenfont;
  int font_codepage;
  int codepage;
  wchar unitab_scoacs[256];
  wchar unitab_line[256];
  wchar unitab_font[256];
  wchar unitab_xterm[256];
  wchar unitab_oemcp[256];
  uchar unitab_ctrl[256];
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
