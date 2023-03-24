#ifndef APPINFO_H
#define APPINFO_H

#define APPNAME "mintty"
#define WEBSITE "http://mintty.github.io/"

#define MAJOR_VERSION  3
#define MINOR_VERSION  6
#define PATCH_NUMBER   4
#define BUILD_NUMBER   0

// needed for res.rc
#define APPDESC "Terminal"
#define AUTHOR  "Thomas Wolff, Andy Koppe"
#define YEAR    "2023"

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
#define COPYRIGHT "© " YEAR " " AUTHOR

// needed for secondary device attributes report
#define DECIMAL_VERSION \
  (MAJOR_VERSION * 10000 + MINOR_VERSION * 100 + PATCH_NUMBER)

// needed for mintty -V and Options... - About...
#ifdef VERSION_SUFFIX
#define VERSION_TEXT \
  APPNAME " " VERSION " (" STRINGIFY(TARGET) ") " STRINGIFY(VERSION_SUFFIX)
#else
#define VERSION_TEXT \
  APPNAME " " VERSION " (" STRINGIFY(TARGET) ")"
#endif

#define LICENSE_TEXT \
  "License GPLv3+: GNU GPL version 3 or later"

#define WARRANTY_TEXT \
  __("There is no warranty, to the extent permitted by law.")

// needed for Options... - About...
//__ %s: WEBSITE (URL)
#define ABOUT_TEXT \
  __("Please report bugs or request enhancements through the " \
  "issue tracker on the mintty project page located at" \
  "\n%s.\n" \
  "See also the Wiki there for further hints, thanks and credits.")

#endif
