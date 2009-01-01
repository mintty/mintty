#ifndef APPINFO_H
#define APPINFO_H

#define xstringify(s) #s
#define stringify(s) xstringify(s)

#define APPNAME "MinTTY"
#define APPVER stringify(VERSION)
#define APPDESC "Cygwin command line window"
#define COPYRIGHT "Copyright (C) 2008-09 Andy Koppe"
#define WEBSITE "http://code.google.com/p/mintty"

#define APPINFO \
  "This software is released under the terms of the\n" \
  "GNU General Public License v3 or later.\n" \
  "\n" \
  "Thanks to Simon Tatham and the other contributors for\n"\
  "their great work on PuTTY, which MinTTY is largely based on.\n" \
  "Thanks also to KDE's Oxygen team for the program icon.\n" \
  "\n" \
  "Updates and sources are available from the project page at\n" \
  WEBSITE ". Do you want to go there now?"

#endif
