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
  {20866, "KOI8-R"},
  {21866, "KOI8-U"}
};

uint
cp_lookup(const char *name)
{
  char buf[strlen(name)];
  uint len;
  for (len = 0; name[len] && name [len] > ' '; len++)
    buf[len] = tolower((uchar)name[len]);
  buf[len] = 0;

  for (uint i = 0; i < lengthof(cp_names); i++) {
    bool found(const char *name) {
      for (uint j = 0; j < len; j++) {
        if (buf[j] != tolower((uchar)name[j]))
          return false;
      }
      return true;
    }
    if (found(cp_names[i].name))
      return cp_names[i].id;
  }
  
  uint id;
  if (sscanf(buf, "iso-8859-%u", &id) == 1 ||
      sscanf(buf, "iso8859-%u", &id) == 1) {
    if (id != 0 && id != 12 && id <= 16)
      return id + 28590;
  }
  else if (sscanf(buf, "cp%u", &id) == 1 ||
           sscanf(buf, "win%u", &id) == 1 ||
           sscanf(buf, "windows-%u", &id) == 1 ||
           sscanf(buf, "%u", &id) == 1) {
    CPINFO cpi;
    if (GetCPInfo(id, &cpi))
      return id;
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
  else if (id != 0)
    sprintf(buf, "CP%u", id);
  else
    *buf = 0;
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

char *
default_locale(void)
{
  static char buf[6] = "xx_XX";
  GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, 2);
  GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf + 3, 2);
  return buf;
}
