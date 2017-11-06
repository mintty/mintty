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

extern string getlocenvcat(string category);

extern void cs_init(void);
extern void cs_reconfig(void);

extern string cs_lang(void);

extern string cs_get_locale(void);
extern void cs_set_locale(string);

typedef enum { CSM_DEFAULT, CSM_OEM, CSM_UTF8 } cs_mode;
extern void cs_set_mode(cs_mode);

extern int cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen);
extern int cs_wcstombs(char *s, const wchar *ws, size_t len);
extern int cs_mbstowcs(wchar *ws, const char *s, size_t wlen);
extern int cs_mb1towc(wchar *pwc, char c);
extern wchar cs_btowc_glyph(char);

extern bool nonascii(string s);
extern char * cs__wcstombs(const wchar * ws);
extern char * cs__wcstombs_dropill(const wchar * ws);
extern char * cs__wcstoutf(const wchar * ws);
extern wchar * cs__mbstowcs(const char * s);
extern wchar * cs__utftowcs(const char * s);
extern wchar * cs__utforansitowcs(const char * s);
extern char * cs__utftombs(char * s);

extern string locale_menu[];
extern string charset_menu[];

extern int cs_cur_max;

extern bool font_ambig_wide;

#if HAS_LOCALES
extern bool cs_ambig_wide;
#else
#define cs_ambig_wide font_ambig_wide
#endif
extern int xcwidth(xchar c);

extern bool indicwide(xchar c);
extern bool extrawide(xchar c);
extern bool combiningdouble(xchar c);
extern bool ambigwide(xchar c);


// path conversions
extern char * path_win_w_to_posix(const wchar * wp);
extern wchar * path_posix_to_win_w(const char * p);
extern char * path_posix_to_win_a(const char * p);


// wchar functions

#define dont_debug_wcs

#if defined(debug_wcs)
#define wcslen _wcslen
#define wcscmp _wcscmp
//CYGWIN_VERSION_API_MINOR < 74
#define wcschr _wcschr
#define wcsrchr _wcsrchr
#define wcsncmp _wcsncmp
#define wcsncpy _wcsncpy
//CYGWIN_VERSION_API_MINOR < 207
#define wcsdup _wcsdup
#endif

#if defined(debug_wcs)

extern unsigned int wcslen(const wchar * s);
extern int wcscmp(const wchar * s1, const wchar * s2);

#endif

#if CYGWIN_VERSION_API_MINOR < 74 || defined(debug_wcs)
// needed for MinGW MSYS

#define wcscpy(tgt, src) memcpy(tgt, src, (wcslen(src) + 1) * sizeof(wchar))
#define wcscat(tgt, src) wcscpy(&tgt[wcslen(tgt)], src)
extern wchar * wcschr(const wchar * s, wchar c);
extern wchar * wcsrchr(const wchar * s, wchar c);
extern int wcsncmp(const wchar * s1, const wchar * s2, int len);
extern wchar * wcsncpy(wchar * s1, const wchar * s2, int len);

#endif

#if CYGWIN_VERSION_API_MINOR < 207 || defined(debug_wcs)

extern wchar * wcsdup(const wchar * s);

#endif


#endif
