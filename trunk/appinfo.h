#ifndef APPINFO_H
#define APPINFO_H

#define xstringify(s) #s
#define stringify(s) xstringify(s)

#define APPNAME "MinTTY"
#define APPVER stringify(VERSION)
#define APPDESC "Cygwin terminal"
#define COPYRIGHT "Copyright (C) 2008-09 Andy Koppe"
#define WEBSITE "http://mintty.googlecode.com"

#define APPINFO \
  "This software is released under the terms of the\n" \
  "GNU General Public License v3 or later.\n" \
  "\n" \
  "Thanks to Simon Tatham and the other contributors for their\n"\
  "great work on PuTTY, which MinTTY is largely based on.\n" \
  "Thanks also to KDE's Oxygen team for the program icon.\n" \
  "\n" \
  "Please report bugs or request enhancements through the\n" \
  "issue tracker on the MinTTY project page located at\n" \
  WEBSITE "."

#endif
