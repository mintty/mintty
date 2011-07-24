PROGS=$(/bin/cygpath -P $CYGWINFORALL)
/bin/mkdir -p "$PROGS/Cygwin" &&
cd "$PROGS/Cygwin" &&
/bin/mkshortcut -n mintty /bin/mintty -a - -d "Cygwin Terminal"
