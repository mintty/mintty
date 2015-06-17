Thanks to everyone who has contributed to mintty in some way, in particular:

  * [Simon Tatham](http://www.chiark.greenend.org.uk/~sgtatham) and the [PuTTY team](http://www.chiark.greenend.org.uk/~sgtatham/putty/team.html). Mintty is based on PuTTY's terminal emulation and Windows frontend parts.
  * Mark Edgar, author of the [PuTTYcyg](http://code.google.com/p/puttycyg) patch for using PuTTY as a Cygwin terminal, which provided much inspiration. Mintty actually started as a hacked PuTTYcyg (although its code for hooking into Cygwin was fully replaced along the way).
  * [KDE](http://www.kde.org)'s [Oxygen](http://www.oxygen-icons.org) team, for the sleek program icon (which was designed for [Konsole](http://konsole.kde.org)).
  * [Markus Kuhn](http://www.cl.cam.ac.uk/~mgk25), for his free [advice](http://www.cl.cam.ac.uk/~mgk25/unicode.html), [code](http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c) and [tests](http://www.cl.cam.ac.uk/~mgk25/ucs/examples) for supporting Unicode.
  * Ahmad Khalifa, for PuTTY's [BiDI](http://en.wikipedia.org/wiki/Bi-directional_text) (bi-directional text) implementation.
  * [Michael Kaplan](http://blogs.msdn.com/b/michkap), Microsoft's [Dr International](http://blogs.msdn.com/b/michkap/archive/2010/01/29/9955550.aspx), for the wealth of information on his [blog](http://blogs.msdn.com/b/michkap), with many gems such as _[He's dead (keys), Jim](http://blogs.msdn.com/b/michkap/archive/2005/01/19/355870.aspx)_.
  * [Thomas Dickey](http://invisible-island.net), for setting the de-facto terminal standard for Unix terminals with [xterm](http://invisible-island.net/xterm/xterm.html), documenting it in [lots of detail](http://visible-island.net/xterm/ctlseqs/ctlseqs.html), and being very responsive to questions and suggestions.
  * The authors of Cygwin's [rxvt](http://sourceforge.net/projects/rxvt), particularly Chuck Wilson, who adapted it to use Win32 directly instead of requiring an X server. While no longer developed, rxvt remains an important reference for how a Cygwin terminal should work.
  * Corinna Vinschen, for Cygwin 1.7's locale and charset implementation, which allowed mintty to gain a crucial advantage over rxvt: Unicode support.
  * Christopher Faylor, for the Cygwin [pty](http://en.wikipedia.org/wiki/Pseudo_terminal) implementation, which mintty very much depends on, especially for its speed.
  * [Simon Steele](http://untidy.net), for [Programmer's Notepad](http://www.pnotepad.org), a relatively simple yet powerful open source editor for Windows that makes mintty code a joy to work on.
  * Murray Anderson, Andrew Aylett and Alexander Noack: mintty's earliest adopters, who helped to find and fix the most egregious bugs before mintty was announced to the Cygwin world.
  * Yaakov Selkowitz, for creating mintty's initial Cygwin [setup.exe](http://cygwin.com/setup.exe) package and the cygport tool that is used to build it.
  * Lee D. Rothstein, for his help in writing a man page for mintty.
  * Thomas Wolff, author of the [mined](http://towo.net/mined) Unicode editor, for lots of much-needed help in improving mintty's terminal emulation and locale support and making it more xterm-compatible.
  * Chris Sutcliffe, for packaging mintty for [MSYS](http://www.mingw.org/wiki/MSYS), and always testing the latest broken svn versions.
  * Iwamuro Motonori, for valuable advice regarding support for East Asian languages.
  * The many users who have helped to improve mintty by reporting bugs and suggesting enhancements.

Cheers,<br>
Andy