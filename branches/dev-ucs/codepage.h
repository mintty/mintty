#ifndef CODEPAGE_H
#define CODEPAGE_H

int decode_codepage(char *cp_name);
const char *cp_enumerate(int index);
const char *cp_name(int codepage);

#endif
