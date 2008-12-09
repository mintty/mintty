#ifndef APPINFO_H
#define APPINFO_H

#define xstringify(s) #s
#define stringify(s) xstringify(s)

#define APPNAME "MinTTY"
#define APPDESC "Cygwin command line window"
#define COPYRIGHT "Copyright (C) 2008 Andy Koppe"
#define VERSION stringify(REVISION)
#define WEBSITE "http://code.google.com/p/mintty"

#endif
