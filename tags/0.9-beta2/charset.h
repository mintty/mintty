#ifndef CHARSET_H
#define CHARSET_H

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
  #define HAS_LOCALES 1
#else
  #define HAS_LOCALES 0
  typedef uint xchar;
  int xcwidth(xchar c);
#endif

const char *cs_init(void);
void cs_reconfig(void);

const char *cs_get_locale(void);
void cs_set_locale(const char *);

typedef enum { CSM_DEFAULT, CSM_OEM, CSM_UTF8 } cs_mode;
void cs_set_mode(cs_mode);

int cs_cur_max;
bool cs_ambig_wide;

int cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
int cs_mbstowcs(wchar *ws, const char *s, size_t wlen);
int cs_mb1towc(wchar *pwc, char c);
wchar cs_btowc_glyph(char);

extern const char *locale_menu[];
extern const char *charset_menu[];

#endif
