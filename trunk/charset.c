// charset.c (part of mintty)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"

#include <locale.h>
#include <langinfo.h>

#include <winbase.h>
#include <winnls.h>

// Constant for representing an unspecified charset.
enum { CS_DEFAULT = -1 };

// ASCII codepage number
enum { CP_ASCII = 20127 }; 

static cs_mode mode = CSM_DEFAULT;

static const char *env_locale, *config_locale, *set_locale;

static const char *default_locale;
static uint default_codepage;

static uint codepage;
static wchar cp_default_wchar;
static char cp_default_char[4];

#if HAS_LOCALES
static bool valid_default_locale, use_locale;
#endif

bool cs_ambig_wide;
int cs_cur_max;

extern bool font_ambig_wide;

static const struct {
  ushort id;
  const char *name;
}
cs_names[] = {
  { CP_UTF8, "UTF-8"},
  { CP_UTF8, "UTF8"},
  {CP_ASCII, "ASCII"},
  {CP_ASCII, "US-ASCII"},
  {CP_ASCII, "ANSI_X3.4-1968"},
  {   20866, "KOI8-R"},
  {   20866, "KOI8R"},
  {   20866, "KOI8"},
  {   21866, "KOI8-U"},
  {   21866, "KOI8U"},
#if HAS_LOCALES
  {   20933, "eucJP"}, // eucJP isn't quite the same as CP20932.
  {   20933, "EUC-JP"},
#endif
  {     620, "TIS620"},
  {     620, "TIS-620"},
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
  ushort id;
  const char *desc;
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
  {   20933, "Japanese"},
#endif
  {     949, "Korean"},
};

const char *locale_menu[8];
const char *charset_menu[lengthof(cs_descs) + 4];

static void
strtoupper(char *dst, const char *src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

static const char *
cs_name(int id)
{
  if (id == CS_DEFAULT)
    return "";

  for (uint i = 0; i < lengthof(cs_names); i++) {
    if (id == cs_names[i].id)
      return cs_names[i].name;
  }
  
  static char buf[16];
  if (id >= 28591 && id <= 28606)
    sprintf(buf, "ISO-8859-%u", id - 28590);
  else
    sprintf(buf, "CP%u", id);
  return buf;
}

static bool
valid_cp(uint cp)
{
  // Check whether Windows knows a codepage.
  CPINFO cpi;
  return GetCPInfo(cp, &cpi);
}

static int
cs_cp(uint id)
{
  // Return codepage number for a charset id.
  switch (id) {
    when CS_DEFAULT: return CP_ACP;
    when 620: return 874;
    when 20933: return 20932;
    otherwise: return id;
  }
}

static bool
valid_cs(int id)
{
  #if HAS_LOCALES
  // Cygwin 1.7 always supports all the ISO charsets.
  if (id >= 28591 && id <= 28606 && id != 28602)
    return true;
  #endif
  return valid_cp(cs_cp(id));
}

static int
cs_id(const char *name)
{
  if (!*name)
    return CS_DEFAULT;
  
  int id = CS_DEFAULT;
  
  char upname[strlen(name) + 1];
  strtoupper(upname, name);
  uint iso;
  if (sscanf(upname, "ISO-8859-%u", &iso) == 1 ||
      sscanf(upname, "ISO8859-%u", &iso) == 1 ||
      sscanf(upname, "ISO8859%u", &iso) == 1) {
    if (iso && iso <= 16 && iso != 12)
      id = 28590 + iso;
  }
  else if (sscanf(upname, "CP%u", &id) != 1 &&
           sscanf(upname, "WIN%u", &id) != 1 &&
           sscanf(upname, "%u", &id) != 1) {
    for (uint i = 0; i < lengthof(cs_names); i++) {
      char cs_upname[8];
      strtoupper(cs_upname, cs_names[i].name);
      if (memcmp(upname, cs_upname, strlen(cs_upname)) == 0) {
        id = cs_names[i].id;
        break;
      }
    }
  }
  
  return valid_cs(id) ? id : CS_DEFAULT;
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
  
  HMODULE kernel = LoadLibrary("kernel32");
  LANGID WINAPI (*pGetUserDefaultUILanguage)(void) = 
    (void *)GetProcAddress(kernel, "GetUserDefaultUILanguage");
  LANGID WINAPI (*pGetSystemDefaultUILanguage)(void) = 
    (void *)GetProcAddress(kernel, "GetSystemDefaultUILanguage");
  
  locale_menu[count++] = "(Env)";
  if (pGetUserDefaultUILanguage)
    add_lcid(pGetUserDefaultUILanguage());
  add_lcid(LOCALE_USER_DEFAULT);
  add_lcid(LOCALE_SYSTEM_DEFAULT);
  if (pGetSystemDefaultUILanguage)
    add_lcid(pGetSystemDefaultUILanguage());
  locale_menu[count++] = "C";
}

static void
init_charset_menu(void)
{
  charset_menu[0] = "(Default)";
  
  const char **p = charset_menu + 1;
  for (uint i = 0; i < lengthof(cs_descs); i++) {
    uint id = cs_descs[i].id;
    if (valid_cs(id))
      asprintf((char **)p++, "%s (%s)", cs_name(id), cs_descs[i].desc);
  }
  
  const char *oem_cs = cs_name(GetOEMCP());
  if (*oem_cs == 'C')
    asprintf((char **)p++, "%s (OEM codepage)", oem_cs);

  const char *ansi_cs = cs_name(GetACP());
  if (*ansi_cs == 'C')
    asprintf((char **)p++, "%s (ANSI codepage)", ansi_cs);
}

static void
get_cp_info(void)
{
  CPINFOEX cpinfo;
  GetCPInfoEx(codepage, 0, &cpinfo);
  cs_cur_max = cpinfo.MaxCharSize;

  cp_default_wchar = cpinfo.UnicodeDefaultChar;
  int len = WideCharToMultiByte(codepage, 0, &cp_default_wchar, 1,
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
    use_default_locale
    ? default_locale
    : cs_ambig_wide ? "ja_JP.UTF-8" : "C.UTF-8"
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
  default_locale = set_locale ?: config_locale ?: env_locale;

  const char *charset;
#if HAS_LOCALES
  valid_default_locale = setlocale(LC_CTYPE, default_locale);
  if (valid_default_locale) {
    charset = nl_langinfo(CODESET);
    cs_ambig_wide = wcwidth(0x3B1) == 2;
  }
  else {
#endif
    const char *dot = strchr(default_locale, '.');
    charset = dot ? dot + 1 : "";
    cs_ambig_wide = font_ambig_wide;
#if HAS_LOCALES
  }
#endif
  int cp = cs_cp(cs_id(charset));
  default_codepage = valid_cp(cp) ? cp : CP_ASCII;
  
  update_mode();
}

const char *
cs_get_locale(void)
{
  return default_locale;
}

void
cs_set_locale(const char *locale)
{
  free((void *)set_locale);
  set_locale = *locale ? strdup(locale) : 0;
  update_locale();
}

void
cs_config_locale(void)
{
  if (*cfg.locale) {
    static char buf[32];
    sprintf(buf, "%s%s%s", cfg.locale, *cfg.charset ? "." : "", cfg.charset);
    config_locale = buf;
#if HAS_LOCALES
    if (setlocale(LC_CTYPE, buf) &&
        wcwidth(0x3B1) == 2 && !font_ambig_wide) {
      // Attach "@cjknarrow" to locale if using an ambig-narrow font
      // with an ambig-wide locale setting
      strcat(buf, "@cjknarrow");
    }
#endif
  }
  else
    config_locale = 0;
  
  update_locale();
}

const char *
cs_init(void)
{
  init_locale_menu();
  init_charset_menu();
  
  // Get locale set in environment or Cygwin default
  const char *
  getlocenv(const char *name)
  {
    const char *val = getenv(name);
    return val && *val ? val : 0;
  }
  const char *locenv =
    getlocenv("LC_ALL") ?: getlocenv("LC_CTYPE") ?: getlocenv("LANG");
  env_locale = strdup(locenv ?: setlocale(LC_CTYPE, ""));
  cs_config_locale();
  const char *lang = config_locale;
#if HAS_LOCALES
  if (!lang && !locenv)
    lang = env_locale;
#endif  
  return lang;
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
cs_mbstowcs(wchar *ws, const char *s, size_t wlen)
{
#if HAS_LOCALES
  if (use_locale)
    return mbstowcs(ws, s, wlen);
#endif
  return MultiByteToWideChar(codepage, 0, s, -1, ws, wlen) - 1;
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
