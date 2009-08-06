// unicode.c (part of MinTTY)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "codepage.h"

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
    if (GetCPInfo(id, &cpi))
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
