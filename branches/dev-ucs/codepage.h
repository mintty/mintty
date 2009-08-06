#ifndef CODEPAGE_H
#define CODEPAGE_H

uint cp_default(void);
uint cp_lookup(const char *);
const char *cp_name(uint id);
const char *cp_enumerate(uint index);

#endif
