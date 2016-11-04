// charset.c (part of mintty)
// Copyright 2008-11 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"

#if HAS_LOCALES
#include <locale.h>
#include <langinfo.h>
#endif

#include <winbase.h>
#include <winnls.h>


static cs_mode mode = CSM_DEFAULT;

static string default_locale;  // Used unless UTF-8 or ACP mode is on.

static string term_locale;     // Locale set via terminal control sequences.
static string config_locale;   // Locale configured in the options.
static string env_locale;      // Locale determined by the environment.
#if HAS_LOCALES
static bool valid_default_locale, use_locale;
bool cs_ambig_wide;
#endif

static uint codepage, default_codepage;

static wchar cp_default_wchar;
static char cp_default_char[4];

int cs_cur_max;

static const struct {
  ushort cp;
  string name;
}
cs_names[] = {
  { CP_UTF8, "UTF-8"},
  { CP_UTF8, "UTF8"},
  {   20127, "ASCII"},
  {   20127, "US-ASCII"},
  {   20127, "ANSI_X3.4-1968"},
  {   20866, "KOI8-R"},
  {   20866, "KOI8R"},
  {   20866, "KOI8"},
  {   21866, "KOI8-U"},
  {   21866, "KOI8U"},
  {   20932, "eucJP"},
  {   20932, "EUC-JP"},
  {     874, "CP874"},
  {     874, "TIS620"},
  {     874, "TIS-620"},
  {     932, "SJIS"},
  {     932, "shift_jis"},
  {     936, "GBK"},
  {     936, "GB2312"},
  {     936, "eucCN"},
  {     936, "EUC-CN"},
  {     949, "eucKR"},
  {     949, "EUC-KR"},
  {     950, "Big5"},
  // Not (yet) supported by Cygwin
  {    1361, "JOHAB"},       // Korean
  {    1361, "KSC5601"},
  {   54936, "GB18030"},
  {  CP_ACP, "ANSI"},
  {  CP_ACP, "ACP"},
  {CP_OEMCP, "OEM"},
  {CP_OEMCP, "OCP"},
};

static const struct {
  ushort cp;
  string desc;
}
cs_descs[] = {
  { CP_UTF8, "Unicode"},
  {   28591, "Western European"},
  {   28592, "Central European"},
  {   28593, "South European"},
  {   28594, "North European"},
  {   28595, "Cyrillic"},
  {   28596, "Arabic"},
  {   28597, "Greek"},
  {   28598, "Hebrew"},
  {   28599, "Turkish"},
#if HAS_LOCALES
  {   28600, "Nordic"},
  {   28601, "Thai"},
#endif
  {   28603, "Baltic"},
#if HAS_LOCALES
  {   28604, "Celtic"},
#endif
  {   28605, "\"euro\""},
#if HAS_LOCALES
  {   28606, "Balkans"},
#endif
  {   20866, "Russian"},
  {   21866, "Ukrainian"},
  {     936, "Chinese"},
  {     950, "Chinese"},
  {     932, "Japanese"},
#if HAS_LOCALES
  {   20932, "Japanese"},
#endif
  {     949, "Korean"},
};

string locale_menu[8];
string charset_menu[lengthof(cs_descs) + 4];

static void
strtoupper(char *dst, string src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

// Return the charset name for a codepage number.
static string
cs_name(uint cp)
{
  for (uint i = 0; i < lengthof(cs_names); i++) {
    if (cp == cs_names[i].cp)
      return cs_names[i].name;
  }

  static char buf[16];
  if (cp >= 28591 && cp <= 28606)
    sprintf(buf, "ISO-8859-%u", cp - 28590);
  else
    sprintf(buf, "CP%u", cp);
  return buf;
}

// Check whether a codepage is installed.
static bool
valid_codepage(uint cp)
{
  CPINFO cpi;
  return GetCPInfo(cp, &cpi);
}

// Find the codepage number for a charset name.
static uint
cs_codepage(string name)
{
  uint cp = CP_ACP;
  char upname[strlen(name) + 1];
  strtoupper(upname, name);
  uint iso;
  if (sscanf(upname, "ISO-8859-%u", &iso) == 1 ||
      sscanf(upname, "ISO8859-%u", &iso) == 1 ||
      sscanf(upname, "ISO8859%u", &iso) == 1) {
    if (iso && iso <= 16 && iso != 12)
      cp = 28590 + iso;
  }
  else if (sscanf(upname, "CP%u", &cp) == 1 ||
           sscanf(upname, "WIN%u", &cp) == 1 ||
           sscanf(upname, "%u", &cp) == 1) {
    // Got a codepage number.
  }
  else {
    // Search the charset table.
    for (uint i = 0; i < lengthof(cs_names); i++) {
      if (!strcasecmp(name, cs_names[i].name)) {
        cp = cs_names[i].cp;
        break;
      }
    }
  }

  return
    cp == CP_ACP ? GetACP() :
    cp == CP_OEMCP ? GetOEMCP() :
    valid_codepage(cp) ? cp : GetACP();
}

static void
init_locale_menu(void)
{
  uint count = 0;

  void add_lcid(LCID lcid) {
    char locale[8];
    int lang_len = GetLocaleInfo(lcid, LOCALE_SISO639LANGNAME,
                                 locale, sizeof locale);
    if (!lang_len)
      return;
    if (GetLocaleInfo(lcid, LOCALE_SISO3166CTRYNAME,
                      locale + lang_len, sizeof locale - lang_len))
      locale[lang_len - 1] = '_';
    for (uint i = 1; i < count; i++)
      if (!strcmp(locale, locale_menu[i]))
        return;
    locale_menu[count++] = strdup(locale);
  }

  locale_menu[count++] = _("(Default)");
  add_lcid(GetUserDefaultUILanguage());
  add_lcid(LOCALE_USER_DEFAULT);
  add_lcid(LOCALE_SYSTEM_DEFAULT);
  add_lcid(GetSystemDefaultUILanguage());
  locale_menu[count++] = "C";
}

static void
init_charset_menu(void)
{
  charset_menu[0] = _("(Default)");

  string *p = charset_menu + 1;
  for (uint i = 0; i < lengthof(cs_descs); i++) {
    uint cp = cs_descs[i].cp;
    if (valid_codepage(cp) || (28591 <= cp && cp <= 28606))
      *p++ = asform("%s (%s)", cs_name(cp), cs_descs[i].desc);
  }

  string oem_cs = cs_name(GetOEMCP());
  if (*oem_cs == 'C')
    *p++ = asform("%s %s", oem_cs, _("(OEM codepage)"));

  string ansi_cs = cs_name(GetACP());
  if (*ansi_cs == 'C')
    *p++ = asform("%s %s", ansi_cs, _("(ANSI codepage)"));
}

static void
get_cp_info(void)
{
  CPINFOEXW cpi;
  GetCPInfoExW(codepage, 0, &cpi);
  cs_cur_max = cpi.MaxCharSize;
  cp_default_wchar = cpi.UnicodeDefaultChar;
  int len =
    WideCharToMultiByte(codepage, 0, &cp_default_wchar, 1,
                        cp_default_char, sizeof cp_default_char - 1, 0, 0);
  cp_default_char[len] = 0;
}

static void
update_mode(void)
{
  codepage =
    mode == CSM_UTF8 ? CP_UTF8 : mode == CSM_OEM  ? 437 : default_codepage;

#if HAS_LOCALES
  bool use_default_locale = mode == CSM_DEFAULT && valid_default_locale;
  setlocale(LC_CTYPE,
    mode == CSM_OEM ? "C.CP437" :
    use_default_locale ? default_locale :
    cs_ambig_wide ? "ja_JP.UTF-8" : "C.UTF-8"
  );
  use_locale = use_default_locale || mode == CSM_UTF8;
  if (use_locale)
    cs_cur_max = MB_CUR_MAX;
  else
    get_cp_info();
#else
  get_cp_info();
#endif

  // Clear output conversion state.
  cs_mb1towc(0, 0);
}

void
cs_set_mode(cs_mode new_mode)
{
  if (new_mode == mode)
    return;
  mode = new_mode;
  update_mode();
}

static void
update_locale(void)
{
  delete(default_locale);

  string locale = term_locale ?: config_locale ?: env_locale;
  string dot = strchr(locale, '.');
  string charset = dot ? dot + 1 : locale;

#if HAS_LOCALES
  string set_locale = setlocale(LC_CTYPE, locale);
  if (!set_locale) {
    locale = asform("C.%s", charset);
    set_locale = setlocale(LC_CTYPE, locale);
    delete(locale);
  }

  valid_default_locale = set_locale;
  if (valid_default_locale) {
    default_codepage = cs_codepage(nl_langinfo(CODESET));
    default_locale = strdup(set_locale);
    cs_ambig_wide = wcwidth(0x3B1) == 2;
  }
  else {
#endif
    default_codepage = cs_codepage(charset);
    default_locale = asform("C.%u", default_codepage);
#if HAS_LOCALES
    cs_ambig_wide = font_ambig_wide;
  }
#endif

  update_mode();
}

string
cs_get_locale(void)
{
  return default_locale;
}

void
cs_set_locale(string locale)
{
  delete(term_locale);
  term_locale = *locale ? strdup(locale) : 0;
  update_locale();
}

void
cs_reconfig(void)
{
  delete(config_locale);
  if (*cfg.locale) {
    config_locale =
      asform("%s%s%s", cfg.locale, *cfg.charset ? "." : "", cfg.charset);
#if HAS_LOCALES
    if (setlocale(LC_CTYPE, config_locale) &&
        wcwidth(0x3B1) == 2 && !font_ambig_wide) {
      // Attach "@cjknarrow" to locale if using an ambig-narrow font
      // with an ambig-wide locale setting
      string l = config_locale;
      config_locale = asform("%s@cjknarrow", l);
      delete(l);
    }
#endif
  }
  else
    config_locale = 0;

  update_locale();
}

static string
getlocenv(string name)
{
  string val = getenv(name);
  return val && *val ? val : 0;
}

void
cs_init(void)
{
  init_locale_menu();
  init_charset_menu();

  env_locale =
#if HAS_LOCALES
    setlocale(LC_CTYPE, "") ?:
#endif
    getlocenv("LC_ALL") ?: getlocenv("LC_CTYPE") ?: getlocenv("LANG");

  env_locale = env_locale ? strdup(env_locale) : "C";

  cs_reconfig();
}

string
cs_lang(void)
{
  return config_locale;
}

int
cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen)
{
#if HAS_LOCALES
  if (use_locale) {
    // The POSIX way
    size_t i = 0, wi = 0;
    len -= MB_CUR_MAX;
    while (wi < wlen && i <= len) {
      int n = wctomb(&s[i], ws[wi++]);
      // Drop untranslatable characters.
      if (n >= 0)
        i += n;
    }
    return i;
  }
#endif
  return WideCharToMultiByte(codepage, 0, ws, wlen, s, len, 0, 0);
}

int
cs_wcstombs(char *s, const wchar *ws, size_t len)
{
  size_t mbslen = cs_wcntombn(s, ws, len, wcslen(ws));
  if (mbslen >= len)
    mbslen = len - 1;
  s[mbslen] = '\0';
  return mbslen;
}

int
cs_mbstowcs(wchar *ws, const char *s, size_t wlen)
{
#if HAS_LOCALES
  if (use_locale)
    return mbstowcs(ws, s, wlen);
#endif
  return MultiByteToWideChar(codepage, 0, s, -1, ws, wlen) - 1;
}

bool
nonascii(string s)
{
  if (!s)
    return false;
  while (*s) {
    if (*s++ & 0x80)
      return true;
  }
  return false;
}

char *
cs__wcstoutf(const wchar * ws)
{
  int size1 = WideCharToMultiByte(CP_UTF8, 0, ws, -1, 0, 0, 0, 0);
  char * s = malloc(size1);  // includes terminating NUL
  WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, size1, 0, 0);
  return s;
}

//#define WC_OPT WC_NO_BEST_FIT_CHARS	// broken, may return empty result
#define WC_OPT 0

char *
cs__wcstombs(const wchar * ws)
{
  char defchar = '?';
  char * defcharpoi = (codepage == CP_UTF8 ? 0 : &defchar);
  int size1 = WideCharToMultiByte(codepage, WC_OPT, ws, -1, 0, 0, 0, 0);
  char * s = malloc(size1);  // includes terminating NUL
  WideCharToMultiByte(codepage, WC_OPT, ws, -1, s, size1, defcharpoi, 0);
  return s;
}

char *
cs__wcstombs_dropill(const wchar * ws)
{
  char defchar = '\0';
  char * defcharpoi = (codepage == CP_UTF8 ? 0 : &defchar);
  int illegal = 0;
  int size1 = WideCharToMultiByte(codepage, WC_OPT, ws, -1, 0, 0, 0, 0);
  char * s = malloc(size1);  // includes terminating NUL
  WideCharToMultiByte(codepage, WC_OPT, ws, -1, s, size1, defcharpoi, &illegal);
  if (illegal) {
    int i = 0;
    for (int k = 0; k < size1; k++)
      if (s[k])
        s[i++] = s[k];
    s[i] = '\0';
  }
  return s;
}

wchar *
cs__utftowcs(const char * s)
{
  int size1 = MultiByteToWideChar(CP_UTF8, 0, s, -1, 0, 0);
  wchar * ws = malloc(size1 * sizeof(wchar));  // includes terminating NUL
  MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, size1);
  return ws;
}

wchar *
cs__mbstowcs(const char * s)
{
  int size1 = MultiByteToWideChar(codepage, 0, s, -1, 0, 0);
  wchar * ws = malloc(size1 * sizeof(wchar));  // includes terminating NUL
  MultiByteToWideChar(codepage, 0, s, -1, ws, size1);
  return ws;
}

wchar *
cs__utforansitowcs(const char * s)
{
  int size1 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, 0, 0);
  if (size1 > 0) {
    wchar * ws = malloc(size1 * sizeof(wchar));  // includes terminating NUL
    MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, size1);
    return ws;
  }
  else {
    size1 = MultiByteToWideChar(CP_ACP, 0, s, -1, 0, 0);
    wchar * ws = malloc(size1 * sizeof(wchar));  // includes terminating NUL
    MultiByteToWideChar(CP_ACP, 0, s, -1, ws, size1);
    return ws;
  }
}

char *
cs__utftombs(char * s)
{
//#if CYGWIN_VERSION_API_MINOR >= 66
#if HAS_LOCALES
  bool utf8out = strcmp(nl_langinfo(CODESET), "UTF-8") == 0;
#else
  char * loc = (char *)cs_get_locale();
  if (!loc)
    loc = "";
  bool utf8out = strstr(loc, ".65001");
#endif
  if (utf8out)
    return strdup(s);
  else {
    wchar * w = cs__utftowcs(s);
    char * mbs = cs__wcstombs(w);
    delete(w);
    return mbs;
  }
}

int
cs_mb1towc(wchar *pwc, char c)
{
#if HAS_LOCALES
  if (use_locale)
    return mbrtowc(pwc, &c, 1, 0);
#endif

  // The Windows way
  static int sn;
  static char s[8];
  static wchar ws[2];

  if (!pwc) {
    // Reset state
    sn = 0;
    return 0;
  }
  if (sn < 0) {
    // Leftover surrogate
    *pwc = ws[1];
    sn = 0;
    return 1;
  }
  s[sn++] = c;
  s[sn] = 0;
  switch (MultiByteToWideChar(codepage, 0, s, sn, ws, 2)) {
    when 1: {
      // Incomplete sequences yield the codepage's default character, but so
      // does the default character's very own (valid) sequence.
      // Pre-Vista, DBCS codepages return a null character rather
      // than the default character for incomplete sequences.
      bool incomplete =
        (*ws == cp_default_wchar && strcmp(s, cp_default_char)) ||
        (!*ws && *s);
      if (!incomplete) {
        *pwc = *ws;
        sn = 0;
        return 1;
      }
    }
    when 2:
      if (IS_HIGH_SURROGATE(*ws)) {
        *pwc = *ws;
        sn = -1; // Surrogate pair
        return 0;
      }
      // Special handling for GB18030. Windows considers the first two bytes
      // of a four-byte sequence as an encoding error followed by a digit.
      if (codepage == 54936 && sn == 2 && ws[1] >= '0' && ws[1] <= '9')
        return -2;
      return -1; // Encoding error
  }
  return sn < cs_cur_max ? -2 : -1;
}

wchar
cs_btowc_glyph(char c)
{
  wchar wc = 0;
  MultiByteToWideChar(codepage, MB_USEGLYPHCHARS, &c, 1, &wc, 1);
  return wc;
}

#if defined(__midipix__) || defined(debug_wcs)

unsigned int
wcslen(const wchar * s)
{
  int len = 0;
  while (s && *s++)
    len++;
  return len;
}

int
wcscmp(const wchar * s1, const wchar * s2)
{
  for (int i = 0; ; i++)
    if (s1[i] < s2[i])
      return -1;
    else if (s1[i] > s2[i])
      return 1;
    else if (s1[i] == 0)
      return 0;
  return 0;
}

#endif

#if CYGWIN_VERSION_API_MINOR < 74 || defined(__midipix__) || defined(debug_wcs)
// needed for MinGW MSYS

wchar *
wcschr(const wchar * s, wchar c)
{
  while (* s) {
    if (* s == c)
      return (wchar *)s;
    s ++;
  }
  return 0;
}

wchar *
wcsrchr(const wchar * s, wchar c)
{
  wchar * res = 0;
  while (* s) {
    if (* s == c)
      res = (wchar *)s;
    s ++;
  }
  return res;
}

int
wcsncmp(const wchar * s1, const wchar * s2, int len)
{
  for (int i = 0; i < len; i++)
    if (s1[i] < s2[i])
      return -1;
    else if (s1[i] > s2[i])
      return 1;
    else if (s1[i] == 0)
      return 0;
  return 0;
}

#endif

#if CYGWIN_VERSION_API_MINOR < 207 || defined(__midipix__) || defined(debug_wcs)

wchar *
wcsdup(const wchar * s)
{
  wchar * dup = newn(wchar, wcslen(s) + 1);
  wcscpy(dup, s);
  return dup;
}

#endif

