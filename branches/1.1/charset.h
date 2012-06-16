#ifndef CHARSET_H
#define CHARSET_H

#if CYGWIN_VERSION_DLL_MAJOR >= 1007
  #define HAS_LOCALES 1
#else
  #define HAS_LOCALES 0
#endif

static inline wchar
high_surrogate(xchar xc)
{ return 0xD800 | (((xc - 0x10000) >> 10) & 0x3FF); }

static inline wchar
low_surrogate(xchar xc)
{ return 0xDC00 | (xc & 0x3FF); }

static inline bool
is_high_surrogate(wchar wc)
{ return (wc & 0xFC00) == 0xD800; }

static inline bool
is_low_surrogate(wchar wc)
{ return (wc & 0xFC00) == 0xDC00; }

static inline xchar
combine_surrogates(wchar hwc, wchar lwc)
{ return 0x10000 + ((hwc & 0x3FF) << 10) + (lwc & 0x3FF); }

void cs_init(void);
void cs_reconfig(void);

string cs_lang(void);

string cs_get_locale(void);
void cs_set_locale(string);

typedef enum { CSM_DEFAULT, CSM_OEM, CSM_UTF8 } cs_mode;
void cs_set_mode(cs_mode);

int cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
int cs_mbstowcs(wchar *ws, const char *s, size_t wlen);
int cs_mb1towc(wchar *pwc, char c);
wchar cs_btowc_glyph(char);

extern string locale_menu[];
extern string charset_menu[];

int cs_cur_max;

extern bool font_ambig_wide;

#if HAS_LOCALES
extern bool cs_ambig_wide;
#else
#define cs_ambig_wide font_ambig_wide
int xcwidth(xchar c);
#endif

#endif
