{ PROGS=$(/bin/cygpath -AP)
  rm --interactive=never "$PROGS/Cygwin/mintty.lnk" &&
  rmdir --ignore-fail-on-non-empty "$PROGS/Cygwin"
} ||
{ PROGS=$(/bin/cygpath -P)
  rm --interactive=never "$PROGS/Cygwin/mintty.lnk" &&
  rmdir --ignore-fail-on-non-empty "$PROGS/Cygwin"
}
