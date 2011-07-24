PROGS=$(/bin/cygpath -P $CYGWINFORALL)
rm --interactive=never "$PROGS/Cygwin/mintty.lnk" &&
rmdir --ignore-fail-on-non-empty "$PROGS/Cygwin"
