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
//{ return 0x10000 + ((hwc & 0x3FF) << 10) + (lwc & 0x3FF); }
{ return ((xchar) (hwc - 0xD7C0) << 10) | (lwc & 0x03FF); }

extern string getlocenvcat(string category);

extern void cs_init(void);
extern void cs_reconfig(void);

extern string cs_lang(void);

extern int cs_get_codepage(void);
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

extern bool cs_ambig_wide;
extern bool cs_single_forced;
extern int xcwidth(xchar c);

extern bool indicwide(xchar c);
extern bool extrawide(xchar c);
extern bool combiningdouble(xchar c);
extern bool is_wide(xchar c);
extern bool is_ambig(xchar c);
extern bool is_ambigwide(xchar c);


// path conversions
extern char * path_win_w_to_posix(const wchar * wp);
extern wchar * path_posix_to_win_w(const char * p);
extern char * path_posix_to_win_a(const char * p);


// wchar functions

#define dont_debug_wcs

#if defined(__midipix__) || defined(debug_wcs)
//__midipix__
#define wcslen _wcslen
#define wcsnlen _wcsnlen
#define wcscmp _wcscmp
//CYGWIN_VERSION_API_MINOR < 74
#define wcschr _wcschr
#define wcsrchr _wcsrchr
#define wcsncmp _wcsncmp
#define wcsncpy _wcsncpy
#define wcsncat _wcsncat
//CYGWIN_VERSION_API_MINOR < 207
#define wcsdup _wcsdup
#endif

#if defined(__midipix__) || defined(debug_wcs)

extern unsigned int wcslen(const wchar * s);
extern int wcscmp(const wchar * s1, const wchar * s2);

#endif

#if CYGWIN_VERSION_API_MINOR < 74 || defined(__midipix__) || defined(debug_wcs)
// needed for MinGW MSYS

#define wcscpy(tgt, src) memcpy(tgt, src, (wcslen(src) + 1) * sizeof(wchar))
#define wcscat(tgt, src) wcscpy(&tgt[wcslen(tgt)], src)
extern unsigned int wcsnlen(const wchar * s, unsigned int max);
extern wchar * wcschr(const wchar * s, wchar c);
extern wchar * wcsrchr(const wchar * s, wchar c);
extern int wcsncmp(const wchar * s1, const wchar * s2, int len);
extern wchar * wcsncpy(wchar * s1, const wchar * s2, int len);
extern wchar * wcsncat(wchar * s1, const wchar * s2, int len);

#endif

#if CYGWIN_VERSION_API_MINOR < 207 || defined(__midipix__) || defined(debug_wcs)

extern wchar * wcsdup(const wchar * s);

#endif


#endif
