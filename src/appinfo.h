#ifndef APPINFO_H
#define APPINFO_H

#define APPNAME "mintty"
#define WEBSITE "http://mintty.github.io/"

#define MAJOR_VERSION  2
#define MINOR_VERSION  3
#define PATCH_NUMBER   6
#define BUILD_NUMBER   0

// needed for res.rc
#define APPDESC "Terminal"
#define AUTHOR  "Andy Koppe / Thomas Wolff"
#define YEAR    "2013/2016"


#define CONCAT_(a,b) a##b
#define CONCAT(a,b) CONCAT_(a,b)
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

#if BUILD_NUMBER
  #define VERSION STRINGIFY(MAJOR_VERSION.MINOR_VERSION-CONCAT(beta,BUILD_NUMBER))
#else
  #define VERSION STRINGIFY(MAJOR_VERSION.MINOR_VERSION.PATCH_NUMBER)
#endif

#if defined SVN_DIR && defined SVN_REV	// deprecated
  #undef BUILD_NUMBER
  #define BUILD_NUMBER SVN_REV
  #undef VERSION
  #define VERSION STRINGIFY(svn-SVN_DIR-CONCAT(r,SVN_REV))
#endif


// needed for res.rc
#define POINT_VERSION \
  STRINGIFY(MAJOR_VERSION.MINOR_VERSION.PATCH_NUMBER.BUILD_NUMBER)
#define COMMA_VERSION \
  MAJOR_VERSION,MINOR_VERSION,PATCH_NUMBER,BUILD_NUMBER
#define COPYRIGHT "(C) " YEAR " " AUTHOR

// needed for secondary device attributes report
#define DECIMAL_VERSION \
  (MAJOR_VERSION * 10000 + MINOR_VERSION * 100 + PATCH_NUMBER)

// needed for mintty -V and Options... - About...
#define VERSION_TEXT \
  APPNAME " " VERSION " (" STRINGIFY(TARGET) ")\n" \
  COPYRIGHT "\n" \
  "License GPLv3+: GNU GPL version 3 or later\n" \
  "There is no warranty, to the extent permitted by law.\n"

// needed for Options... - About...
#define ABOUT_TEXT \
  "Thanks to Simon Tatham and the other contributors for their\n"\
  "great work on PuTTY, which mintty is largely based on. Thanks\n" \
  "also to Thomas Wolff and Chris Sutcliffe for lots of testing and\n" \
  "advice, and to KDE's Oxygen team for the program icon.\n" \
  "\n" \
  "Please report bugs or request enhancements through the\n" \
  "issue tracker on the mintty project page located at\n" \
  WEBSITE ".\n"

#endif
