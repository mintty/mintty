PROGS=$(/bin/cygpath -P $CYGWINFORALL)
/bin/mkdir -p "$PROGS/Cygwin" &&
/bin/mkshortcut -n "$PROGS/Cygwin/mintty" -a - -d Terminal /bin/mintty &&
if [ "$CYGWINFORALL" ]; then
  /bin/chmod a+r "$PROGS/Cygwin/mintty.lnk"
fi
