PROGS=$(/bin/cygpath -P $CYGWINFORALL)
/bin/mkdir -p "$PROGS/Cygwin" &&
cd "$PROGS/Cygwin" &&
/bin/mkshortcut -n mintty -a - -d Terminal /bin/mintty &&
if [ "$CYGWINFORALL" ]; then
  /bin/chmod a+r mintty.lnk
fi
