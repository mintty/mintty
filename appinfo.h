#ifndef APPINFO_H
#define APPINFO_H

#define CONCAT_(a,b) a##b
#define CONCAT(a,b) CONCAT_(a,b)
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

#define APPNAME "MinTTY"
#define APPDESC "Cygwin Terminal"
#define WEBSITE "http://mintty.googlecode.com"
#define AUTHOR  "Andy Koppe"
#define YEARS   "2008-09"

#define VERSION "0.5-alpha1"
#define MAJOR_VERSION  0
#define MINOR_VERSION  5
#define PATCH_NUMBER   0
#define RELEASE_NUMBER 1

#if defined BRANCH && defined REVISION
#undef VERSION
#undef RELEASE_NUMBER
#define VERSION STRINGIFY(CONCAT(svn-BRANCH-r,REVISION))
#define RELEASE_NUMBER REVISION
#endif

#define POINT_VERSION \
  STRINGIFY(MAJOR_VERSION.MINOR_VERSION.PATCH_NUMBER.RELEASE_NUMBER)
#define COMMA_VERSION \
  MAJOR_VERSION,MINOR_VERSION,PATCH_NUMBER,RELEASE_NUMBER
#define DECIMAL_VERSION \
  (MAJOR_VERSION * 10000 + MINOR_VERSION * 100 + PATCH_NUMBER)

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
