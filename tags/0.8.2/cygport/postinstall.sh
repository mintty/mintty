{ PROGS=$(/bin/cygpath -AP)
  /bin/mkdir -p "$PROGS/Cygwin" &&
  /bin/mkshortcut -AP -n Cygwin/mintty -a - -d Terminal /bin/mintty.exe &&
  /bin/chmod a+r "$PROGS/Cygwin/mintty.lnk"
} ||
{ /bin/mkdir -p "$(/bin/cygpath -P)/Cygwin" &&
  /bin/mkshortcut -P -n Cygwin/mintty -a - -d Terminal /bin/mintty.exe
}
