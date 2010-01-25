PROGS=$(/bin/cygpath -AP)
if [ ! -w "$PROGS" ]; then
  PROGS=$(/bin/cygpath -P)
fi
rm -f "$PROGS/Cygwin/mintty.lnk"
rmdir --ignore-fail-on-non-empty "$PROGS/Cygwin"
