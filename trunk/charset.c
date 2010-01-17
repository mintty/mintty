// charset.c (part of mintty)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"
#include "platform.h"

#include <locale.h>
#include <winbase.h>
#include <winnls.h>

// Constant for representing an unspecified charset.
#define CS_DEFAULT -1

// ASCII codepage number
#define CP_ASCII 20127

static cs_mode mode = CSM_DEFAULT;

static const char *env_locale, *config_locale, *set_locale;

static const char *default_locale;
static uint default_codepage;

static uint codepage;

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
  {CP_UTF8, "UTF-8"},
  {CP_ASCII, "ASCII"},
  {  20866, "KOI8-R"},
  {  21866, "KOI8-U"},
  {    936, "GBK"},
  {    950, "Big5"},
  {    932, "SJIS"},
#if HAS_LOCALES
  {  20932, "eucJP"},
#endif
  {    949, "eucKR"},
  // Aliases
  {CP_UTF8, "UTF8"},
  {  20866, "KOI8"},
  // Not supported by Cygwin
  {  54936, "GB18030"},
  { CP_ACP, "ANSI"},
  { CP_ACP, "ACP"},
  { CP_OEMCP, "OEM"},
  { CP_OEMCP, "OCP"},
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
  {   20932, "Japanese"},
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

static bool
valid_cs(int id)
{
  #if HAS_LOCALES
  // Cygwin 1.7 always supports all the ISO charsets.
  if (id >= 28591 && id <= 28606 && id != 28602)
    return true;
  #endif
  return valid_cp(id);
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
  if (sscanf(upname, "ISO-8859-%u", &iso) == 1) {
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

void
correct_charset(char *cs)
{
  strcpy(cs, cs_name(cs_id(cs)));
}

void
correct_locale(char *locale)
{
  if (!strcmp(locale, "C"))
    return;
  uchar *lang = (uchar *)locale;
  if (isalpha(lang[0]) && isalpha(lang[1])) {
    // Treat two letters at the start as the language.
    locale[0] = tolower(lang[0]);
    locale[1] = tolower(lang[1]);
    uchar *terr = (uchar *)strchr(locale + 2, '_');
    if (terr && isalpha(terr[1]) && isalpha(terr[2])) {
      // Treat two letters after an underscore as the territory.
      locale[2] = '_';
      locale[3] = toupper(terr[1]);
      locale[4] = toupper(terr[2]);
      locale[5] = 0;
    }
    else
      locale[2] = 0;
  }
  else 
    locale[0] = 0;
}

static void
init_locale_menu(void)
{
  uint count = 0;
  
  void add_lcid(LCID lcid) {
    char locale[6];
    if (!GetLocaleInfo(lcid, LOCALE_SISO639LANGNAME, locale, 3) ||
        !GetLocaleInfo(lcid, LOCALE_SISO3166CTRYNAME, locale + 3, 3))
      return;
    locale[2] = '_';
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
  
  locale_menu[count++] = "(None)";
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

static int
cp_cur_max(void)
{
  CPINFO cpinfo;
  GetCPInfo(codepage, &cpinfo);
  return cpinfo.MaxCharSize;
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
    : cs_ambig_wide ? "ja.UTF-8" : "en.UTF-8"
  );
  use_locale = use_default_locale || mode == CSM_UTF8;
  cs_cur_max = use_locale ? MB_CUR_MAX : cp_cur_max();
#else
  cs_cur_max = cp_cur_max();
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

  const char *dot = strchr(default_locale, '.');
  const char *charset = dot ? dot + 1 : "";
  int id = cs_id(charset);
  if (id != CS_DEFAULT && valid_cp(id))
    default_codepage = id;
#if HAS_LOCALES
  else if (*default_locale == 'C')
    default_codepage = CP_ASCII;
#endif
  else 
    default_codepage = CP_ACP;  
  
#if HAS_LOCALES
  valid_default_locale = setlocale(LC_CTYPE, default_locale);
  if (valid_default_locale)
    cs_ambig_wide = wcwidth(0x3B1) == 2;
  else
    cs_ambig_wide = font_ambig_wide;
#else
  cs_ambig_wide = font_ambig_wide;
#endif
  
  update_mode();
}

const char *
cs_set_locale(const char *locale)
{
  if (locale) {
    free((void *)set_locale);
    set_locale = *locale ? strdup(locale) : 0;
    update_locale();
  }
  return default_locale;
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
cs_mb1towc(wchar *pwc, const char *pc)
{
#if HAS_LOCALES
  if (use_locale)
    return mbrtowc(pwc, pc, 1, 0);
#endif

  // The Windows way
  static int sn;
  static char s[8];
  static wchar ws[2];

  if (!pc) {
    // Reset state
    sn = 0;
    return 0;
  }
  if (sn < 0) {
    // Leftover surrogate
    *pwc = ws[1];
    sn = 0;
    return 0;
  }
  if (sn == cs_cur_max)
    return -1; // Overlong sequence
  s[sn++] = *pc;
  switch (MultiByteToWideChar(codepage, 0, s, sn, ws, 2)) {
    when 1:
      if (*ws == 0xFFFD)
        return -2; // Incomplete character
      else
        sn = 0; // Valid character
    when 2:
      if (*ws == 0xFFFD)
        return -1; // Encoding error
      else
        sn = -1; // Surrogate pair
    when 0:
      return -2; // pre-Vista: can't tell errors from incomplete chars :(
  }
  *pwc = *ws;
  return 1;
}

wchar
cs_btowc_glyph(char c)
{
  wchar wc = 0;
  MultiByteToWideChar(codepage, MB_USEGLYPHCHARS, &c, 1, &wc, 1);
  return wc;
}
