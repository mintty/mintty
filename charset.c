// charset.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"
#include "platform.h"

#include <locale.h>
#include <winbase.h>
#include <winnls.h>

static const struct {
  ushort id;
  const char *name;
}
cs_names[] = {
  {CP_UTF8, "UTF-8"},
  {  20127, "ASCII"},
  {  20866, "KOI8-R"},
  {  21866, "KOI8-U"},
  {    936, "GBK"},
  {    950, "Big5"},
  {    932, "SJIS"},
#if HAS_LOCALES
  {  51932, "eucJP"},  // CP20932 is a simplified DBCS version of the proper one
#endif
  {    949, "eucKR"},
  // Not supported by Cygwin
  {  54396, "GB18030"},
  // Aliases
  {CP_UTF8, "UTF8"},
  {  20866, "KOI8"}
};

static const struct {
  ushort id;
  const char *comment;
}
cs_menu[] = {
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
  {   28600, "Nordic"},
  {   28601, "Thai"},
  {   28603, "Baltic"},
  {   28604, "Celtic"},
  {   28605, "\"euro\""},
  {   28606, "Balkans"},
  {   20866, "Russian"},
  {   21866, "Ukrainian"},
  {     936, "Chinese"},
  {     950, "Chinese"},
  {     932, "Japanese"},
#if HAS_LOCALES
  {   51932, "Japanese"},
#endif
  {     949, "Korean"},
};

static const char *const
locale_menu[] = {
  "ar", // Arabic
  "bn", // Bengali
  "de", // German
  "en", // English
  "es", // Spanish
  "fa", // Persian
  "fr", // French
  "hi", // Hindi
  "id", // Indonesian
  "it", // Italian
  "ja", // Japanese
  "ko", // Korean
  "pt", // Portuguese
  "ru", // Russian
  "th", // Thai
  "tr", // Turkish
  "ur", // Urdu
  "vi", // Vietnamese
  "zh", // Chinese
};

static void
strtoupper(char *dst, const char *src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

static uint
cs_lookup(const char *name)
{
  if (!*name)
    return CP_ACP;

  char upname[strlen(name) + 1];
  strtoupper(upname, name);

  uint id;
  if (sscanf(upname, "ISO-8859-%u", &id) == 1) {
    if (id != 0 && id != 12 && id <= 16)
      return id + 28590;
  }
  else if (sscanf(upname, "CP%u", &id) == 1 ||
           sscanf(upname, "WIN%u", &id) == 1 ||
           sscanf(upname, "%u", &id) == 1) {
    CPINFO cpi;
    if (GetCPInfo(id, &cpi))
      return id;
  }
  else {
    for (uint i = 0; i < lengthof(cs_names); i++) {
      char cs_upname[8];
      strtoupper(cs_upname, cs_names[i].name);
      if (memcmp(upname, cs_upname, strlen(cs_upname)) == 0)
        return cs_names[i].id;
    }
  }
  return CP_ACP;
}

static const char *
cs_name(uint id)
{
  if (id == CP_ACP)
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

void
correct_charset(char *cs)
{
  strcpy(cs, cs_name(cs_lookup(cs)));
}

void
correct_locale(char *locale)
{
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


/*
 * Return the nth code page in the list, for use in the GUI
 * configurer.
 */
 
const char *
enumerate_charsets(uint i)
{
  if (i == 0)
    return "(Default)";
  if (--i < lengthof(cs_menu)) {
    static char buf[64];
    sprintf(buf, "%s (%s)", cs_name(cs_menu[i].id), cs_menu[i].comment);
    return buf;
  }
  return 0;
}

const char *
enumerate_locales(uint i)
{
  if (i == 0)
    return "(None)";
  if (i == 1) {
    static char buf[] = "xx_XX";
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, 2);
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf + 3, 2);
    return buf;
  }
  i -= 2;
  if (i < lengthof(locale_menu))
    return locale_menu[i];
  return 0;
}

static cs_mode mode = CSM_DEFAULT;
static uint default_codepage, codepage;

#if HAS_LOCALES
static const char *default_locale;
static bool use_locale;
#endif

extern bool font_ambig_wide;
bool cs_ambig_wide;
int cs_cur_max;

static int
cp_cur_max(void)
{
  CPINFO cpinfo;
  GetCPInfo(codepage, &cpinfo);
  return cpinfo.MaxCharSize;
}

static void
cs_update(void)
{
  codepage = 
    mode == CSM_UTF8 ? CP_UTF8 : mode == CSM_OEM  ? 437 : default_codepage;

#if HAS_LOCALES
  bool use_default_locale = mode == CSM_DEFAULT && default_locale;
  setlocale(
    LC_CTYPE,
    use_default_locale ? default_locale : cs_ambig_wide ? "ja.UTF-8" : "en.UTF-8"
  );
  use_locale = use_default_locale || mode == CSM_UTF8;  // Not for CSM_OEM
  cs_cur_max = use_locale ? MB_CUR_MAX : cp_cur_max();
#else
  cs_cur_max = cp_cur_max();
#endif

  // Clear output conversion state.
  cs_mb1towc(0, 0);
}

const char *
cs_config(void)
{
  static char locale[32];
  char *lang = *cfg.locale ? locale : 0;  // Resulting LANG seetting
  if (lang) {
    snprintf(
      locale, sizeof locale,
      "%s%s%s", cfg.locale, *cfg.charset ? "." : "", cfg.charset
    );
    default_codepage = cs_lookup(cfg.charset);
  }
  else {
    snprintf(
      locale, sizeof locale,
      getenv("LC_ALL") ?: getenv("LC_CTYPE") ?: getenv("LANG") ?: "C"
    );
    char *dot = strchr(locale, '.');
    default_codepage = dot ? cs_lookup(dot + 1) : CP_ACP;
  }
  
  #if HAS_LOCALES
  default_locale = setlocale(LC_CTYPE, locale) ? locale : 0;
  cs_ambig_wide = default_locale && wcwidth(0x3B1) == 2;
  
  if (lang && cs_ambig_wide && !font_ambig_wide) {
    // Attach "@cjknarrow" to locale if using an ambig-narrow font
    // with an ambig-wide locale setting
    strcat(locale, "@cjknarrow");
    cs_ambig_wide = false;
  }
  #else
  cs_ambig_wide = font_ambig_wide;
  #endif
  
  cs_update();
  
  return lang;
}

void
cs_set_mode(cs_mode new_mode)
{
  if (new_mode != mode) {
    mode = new_mode;
    cs_update();
  }
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
      // Drop characters than can't be translated to charset.
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
