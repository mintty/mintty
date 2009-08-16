// charset.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"

#include <locale.h>
#include <winbase.h>
#include <winnls.h>

static const struct {
  ushort id;
  const char *name;
}
cs_names[] = {
  {65001, "UTF-8"},
  {  936, "GBK"},
  {  950, "Big5"},
  {  932, "SJIS"},
  {20933, "eucJP"},
  {  949, "eucKR"},
  // Not supported by Cygwin
  {20866, "KOI8-R"},
  {21866, "KOI8-U"},
  {54396, "GB18030"},
  // Aliases
  {65001, "UTF8"},
  {20866, "KOI8"}
};

static const struct {
  ushort id;
  const char *comment;
}
cs_menu[] = {
  {65001, "Unicode"},
  {28591, "Western European"},
  {28592, "Central European"},
  {28593, "South European"},
  {28594, "North European"},
  {28595, "Cyrillic"},
  {28596, "Arabic"},
  {28597, "Greek"},
  {28598, "Hebrew"},
  {28599, "Turkish"},
  {28600, "Nordic"},
  {28601, "Thai"},
  {28603, "Baltic"},
  {28604, "Celtic"},
  {28605, "\"euro\""},
  { 1250, "Central European"},
  { 1251, "Cyrillic"},
  { 1252, "Western European"},
  { 1253, "Greek"},
  { 1254, "Turkish"},
  { 1255, "Hebrew"},
  { 1256, "Arabic"},
  { 1257, "Baltic"},
  { 1258, "Vietnamese"},
  {  874, "Thai"},
  {  936, "Simplified Chinese"},
  {  950, "Traditional Chinese"},
  {  932, "Japanese"},
  {20933, "Japanese"},
  {  949, "Korean"}
};

static const char *const
locale_menu[] = {
  0,
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
  ""
};

static void
strtoupper(char *dst, const char *src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

uint
cs_lookup(const char *name)
{
  if (!*name)
    return 0;

  char upname[strlen(name) + 1];
  strtoupper(upname, name);

  uint id;
  if (sscanf(upname, "ISO-8859-%u", &id) == 1) {
    if (id != 0 && id != 12 && id <= 16)
      return id + 28590;
  }
  else if (sscanf(upname, "CP%u", &id) == 1 ||
           sscanf(upname, "WIN%u", &id) == 1) {
    CPINFO cpi;
    if (id >= 100 && GetCPInfo(id, &cpi))
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
  return 0;
}

const char *
cs_name(uint id)
{
  if (!id)
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

/*
 * Return the nth code page in the list, for use in the GUI
 * configurer.
 */
 
const char *
enumerate_charsets(uint i)
{
  if (i >= lengthof(cs_menu))
    return 0;
  static char buf[64];
  sprintf(buf, "%s (%s)", cs_name(cs_menu[i].id), cs_menu[i].comment);
  return buf;
}

const char *
enumerate_locales(uint i)
{
  if (i == 0) {
    static char buf[] = "xx_XX";
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, 2);
    GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf + 3, 2);
    return buf;
  }
  else if (i < lengthof(locale_menu))
    return locale_menu[i];
  else
    return 0;
}

static cs_mode mode = CSM_DEFAULT;
static char default_locale[32], utf8_locale[32];
bool valid_locale;
static uint default_codepage, codepage;

static void
update_locale(void)
{
  char *locale;
  switch (mode) {
    when CSM_OEM:  locale = "C-CP437";      codepage = 437;
    when CSM_UTF8: locale = utf8_locale;    codepage = CP_UTF8;
    otherwise:     locale = default_locale; codepage = default_codepage;
  }
  valid_locale = setlocale(LC_CTYPE, locale);
  cs_mb1towc(0, 0);
}

const char *
cs_config_locale(bool font_ambig_wide)
{
  const char *loc = cfg.locale, *cset = cfg.charset;
  const char *ret = default_locale;
  if (*loc) {
    const char *narrow =
      !font_ambig_wide && setlocale(LC_CTYPE, loc) && wcwidth(0x3B1) == 2
      ? "@cjknarrow" : "";
    if (*cset)
      snprintf(default_locale, 32, "%s.%s%s", loc, cset, narrow);
    else
      snprintf(default_locale, 32, "%s%s", loc, narrow);
    snprintf(utf8_locale, 32, "%s.UTF-8%s", loc, narrow);
  }
  else {
    if (*cset) {
      snprintf(default_locale, 32, "C-%s", cset);
      strcpy(utf8_locale, "C-UTF-8");
    }
    else {
      snprintf(default_locale, 32, setlocale(LC_CTYPE, "") ?: "C");
      bool ambig_wide = font_ambig_wide && wcwidth(0x3B1) == 2;
      snprintf(utf8_locale, 32, "%sUTF-8", ambig_wide ? "ja." : "C-");
      ret = 0;
    }
  }
  default_codepage = cset ? cs_lookup(cset) : 0;
  update_locale();
  return ret;  
}

void
cs_set_mode(cs_mode new_mode)
{
  if (new_mode != mode) {
    mode = new_mode;
    update_locale();
  }
}

int
cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen)
{
  if (!valid_locale)
    return WideCharToMultiByte(codepage, 0, ws, wlen, s, len, 0, 0);

  // The POSIX way
  size_t i = 0, wi = 0;
  while (wi < wlen && i + MB_CUR_MAX < len) {
    int n = wctomb(&s[i], ws[wi++]);
    // Drop characters than can't be translated to charset.
    if (n >= 0)
      i += n;
  }
  return i;
}

int
cs_mbstowcs(wchar *ws, const char *s, size_t wlen)
{
  if (valid_locale)
    return mbstowcs(ws, s, wlen);
  else
    return MultiByteToWideChar(codepage, 0, s, -1, ws, wlen) - 1;
}

int
cs_mb1towc(wchar *pwc, const char *pc)
{
  if (valid_locale)
    return mbrtowc(pwc, pc, 1, 0);

  // The Windows way
  static int sn;
  static char s[MB_LEN_MAX];
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
  if (sn == sizeof s)
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
    otherwise:
      return -1; // Shouldn't happen
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
