// unicode.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "codepage.h"

#include "config.h"

#include <locale.h>
#include <winbase.h>
#include <winnls.h>

static const struct {
  ushort id;
  const char *name;
}
cp_names[] = {
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

static void
strtoupper(char *dst, const char *src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

uint
cp_lookup(const char *name)
{
  if (!*name)
    return GetACP();

  char upname[strlen(name) + 1];
  strtoupper(upname, name);

  uint id;
  if (sscanf(upname, "ISO-8859-%u", &id) == 1) {
    if (id != 0 && id != 12 && id <= 16)
      return id + 28590;
  }
  else if (sscanf(upname, "CP%u", &id) == 1 ||
           sscanf(upname, "WIN%u", &id)) {
    CPINFO cpi;
    if (id > 3 && GetCPInfo(id, &cpi))
      return id;
  }
  else {
    for (uint i = 0; i < lengthof(cp_names); i++) {
      char cp_upname[8];
      strtoupper(cp_upname, cp_names[i].name);
      if (memcmp(upname, cp_upname, strlen(cp_upname)) == 0)
        return cp_names[i].id;
    }
  }
  return 0;
}

const char *
cp_name(uint id)
{
  for (uint i = 0; i < lengthof(cp_names); i++) {
    if (id == cp_names[i].id)
      return cp_names[i].name;
  }
  static char buf[16];
  if (id >= 28591 && id <= 28606)
    sprintf(buf, "ISO-8859-%u", id - 28590);
  else
    sprintf(buf, "CP%u", id ?: GetACP());
  return buf;
}

/*
 * Return the nth code page in the list, for use in the GUI
 * configurer.
 */
static const struct {
  ushort id;
  const char *comment;
}
cp_menu[] = {
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
  {20866, "Russian"},
  {21866, "Ukrainian"},
  {  874, "Thai"},
  {  936, "Simplified Chinese"},
  {  950, "Traditional Chinese"},
  {  932, "Japanese"},
  {20933, "Japanese"},
  {  949, "Korean"}
};

const char *
cp_enumerate(uint i)
{
  if (i >= lengthof(cp_menu))
    return 0;
  static char buf[64];
  sprintf(buf, "%s (%s)", cp_name(cp_menu[i].id), cp_menu[i].comment);
  return buf;
}

void
get_default_locale(char *buf)
{
  strcpy(buf, "xx_XX");
  GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, 2);
  GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf + 3, 2);
}

static int codepage;
static char utf8_locale[32];
static bool utf8_mode = false;

void
cp_set_utf8_mode(bool mode)
{
  utf8_mode = mode;
  char *locale = setlocale(LC_CTYPE, mode ? utf8_locale : "");
  codepage = locale ? 0 : cp_lookup(cfg.codepage);
  cp_btowc(0, 0);
}

void
cp_update_locale(bool ambig_wide)
{
  void
  narrow_locale(char *locale)
  {
    // Add @cjknarrow to locale if we're using an ambig_narrow font with an 
    // ambig_wide locale. (Test on Greek alpha.)
    setlocale(LC_CTYPE, locale);
    if (!ambig_wide && wcwidth(0x3B1) == 2)
      strcat(locale, "@cjknarrow");
  }

  char locale[32];
  sprintf(locale, "%s.%s", cfg.locale, cfg.codepage);
  narrow_locale(locale);
  setenv("LC_CTYPE", locale, true);
  
  sprintf(utf8_locale, "%s.UTF-8", cfg.locale);
  narrow_locale(utf8_locale);
  
  cp_set_utf8_mode(utf8_mode);
}

int
cp_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen)
{
  if (codepage)
    return WideCharToMultiByte(codepage, 0, ws, wlen, s, len, 0, 0);

  // The POSIX way
  size_t i = 0, wi = 0;
  while (wi < wlen && i + MB_CUR_MAX < len) {
    int n = wctomb(&s[i], ws[wi++]);
    // Drop characters than can't be translated to codepage.
    if (n >= 0)
      i += n;
  }
  return i;
}

int
cp_btowc(wchar *pwc, const char *pc)
{
  if (!codepage)
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

int
cp_mbstowcs(wchar *ws, const char *s, size_t wlen)
{
  if (!codepage)
    return mbstowcs(ws, s, wlen);
  else
    return MultiByteToWideChar(codepage, 0, s, -1, ws, wlen) - 1;
}

wchar
cp_oemtowc(char c)
{
  wchar wc;
  MultiByteToWideChar(437, 0, &c, 1, &wc, 1);
  return wc;
}
