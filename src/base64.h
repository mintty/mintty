#ifndef BASE64_H
#define BASE64_H

enum {
  B64_OK,
  B64_OVERFLOW	= -10000,
  B64_INVALID_LEN,
  B64_INVALID_CHAR,
  B64_INTERNAL_ERROR,
  B64_INTERNAL_INVALID_LEN,
};


int base64_encode(const char *input, int ilen, char *output, int olen);
int base64_decode(const char *input, int ilen, char *out, int olen);
int base64_decode_clip(const char *input, int ilen, char *out, int olen);

#endif
