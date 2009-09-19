#ifndef CHARSET_H
#define CHARSET_H

const char *cs_init(void);
void cs_config(void);

typedef enum { CSM_DEFAULT, CSM_OEM, CSM_UTF8 } cs_mode;
void cs_set_mode(cs_mode);

int cs_cur_max;
bool cs_ambig_wide;

int cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
int cs_mbstowcs(wchar *ws, const char *s, size_t wlen);
int cs_mb1towc(wchar *pwc, const char *pc);
wchar cs_btowc_glyph(char);

void correct_charset(char *);
void correct_locale(char *);

extern char system_locale[];

const char *enumerate_locales(uint);
const char *enumerate_charsets(uint);

#endif
