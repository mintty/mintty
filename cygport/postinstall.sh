/usr/bin/mkdir -p "$(/usr/bin/cygpath -AP)/Cygwin"
/usr/bin/mkshortcut -AP -n Cygwin/mintty -a - -d Terminal /usr/bin/mintty.exe
/usr/bin/chmod go+r "$(/usr/bin/cygpath -AP)/Cygwin/mintty.lnk"
