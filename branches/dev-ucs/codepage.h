#ifndef CODEPAGE_H
#define CODEPAGE_H

uint cp_lookup(const char *);
const char *cp_name(uint id);
const char *cp_enumerate(uint index);

void cp_update_locale(bool ambig_wide);
void cp_set_utf8_mode(bool mode);

int cp_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
int cp_btowc(wchar *pwc, const char *pc);
int cp_mbstowcs(wchar *ws, const char *s, size_t wlen);
wchar cp_oemtowc(char c);

#endif
