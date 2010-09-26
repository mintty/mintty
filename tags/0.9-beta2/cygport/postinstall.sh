ALL=$CYGWINFORALL
PROGS=$(/bin/cygpath -P $ALL)
/bin/mkdir -p "$PROGS/Cygwin" &&
/bin/mkshortcut -P $ALL -n Cygwin/mintty -a - -d Terminal /bin/mintty &&
if [ "$ALL" ]; then
  /bin/chmod a+r "$PROGS/Cygwin/mintty.lnk"
fi
