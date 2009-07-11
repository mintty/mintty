#ifndef APPINFO_H
#define APPINFO_H

#define APPNAME "MinTTY"
#define APPDESC "Cygwin Terminal"
#define WEBSITE "http://mintty.googlecode.com"
#define AUTHOR  "Andy Koppe"
#define YEARS   "2008-09"

#define MAJOR_VERSION 0
#define MINOR_VERSION 4
#define PATCH_VERSION 3


#define __CONCAT(a,b) a##b
#define _CONCAT(a,b) __CONCAT(a,b)
#define __STRING(s) #s
#define _STRING(s) __STRING(s)

#if defined REVISION && defined DIRECTORY
#define VERSION _STRING(_CONCAT(svn-DIRECTORY-r,REVISION))
#else
#define VERSION _STRING(MAJOR_VERSION.MINOR_VERSION.PATCH_VERSION)
#define REVISION 0
#endif

#define POINT_VERSION \
  _STRING(MAJOR_VERSION.MINOR_VERSION.PATCH_VERSION.REVISION)
#define COMMA_VERSION \
  MAJOR_VERSION,MINOR_VERSION,PATCH_VERSION,REVISION
#define DECIMAL_VERSION \
  (MAJOR_VERSION * 10000 + MINOR_VERSION * 100 + PATCH_VERSION)

#define COPYRIGHT "(C) " YEARS " " AUTHOR


#define VERSION_TEXT \
  APPNAME " " VERSION "\n" \
  COPYRIGHT "\n" \
  "License GPLv3+: GNU GPL version 3 or later\n" \
  "There is no warranty, to the extent permitted by law."

#define ABOUT_TEXT \
  "Thanks to Simon Tatham and the other contributors for their\n"\
  "great work on PuTTY, which MinTTY is largely based on.\n" \
  "Thanks also to KDE's Oxygen team for the program icon.\n" \
  "\n" \
  "Please report bugs or request enhancements through the\n" \
  "issue tracker on the MinTTY project page located at\n" \
  WEBSITE "."

#endif
