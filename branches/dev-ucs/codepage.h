#ifndef CODEPAGE_H
#define CODEPAGE_H

uint cp_default(void);
uint cp_lookup(const char *);
const char *cp_name(uint id);
const char *cp_enumerate(uint index);

void cp_update_locale(bool ambig_wide);
void cp_set_utf8_mode(bool mode);

#endif
