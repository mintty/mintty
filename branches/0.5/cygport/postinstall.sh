PROGS=$(/bin/cygpath -AP)
if [ ! -w "$PROGS" ]; then
  PROGS=$(/bin/cygpath -P)
fi
/bin/mkdir -p "$PROGS/Cygwin"
/bin/mkshortcut -n "$PROGS/Cygwin/mintty" -a - -d Terminal /bin/mintty.exe
/bin/chmod go+r "$PROGS/Cygwin/mintty.lnk"
