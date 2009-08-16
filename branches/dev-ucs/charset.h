#ifndef CODEPAGE_H
#define CODEPAGE_H

const char *cs_config_locale(bool ambig_wide);

typedef enum { CSM_DEFAULT, CSM_OEM, CSM_UTF8 } cs_mode;
void cs_set_mode(cs_mode);

int cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
int cs_btowc(wchar *pwc, const char *pc);
int cs_mbstowcs(wchar *ws, const char *s, size_t wlen);

uint cs_lookup(const char *);
const char *cs_name(uint id);

const char *enumerate_locales(uint);
const char *enumerate_charsets(uint);

#endif
