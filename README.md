Mintty is a [terminal emulator](http://en.wikipedia.org/wiki/Terminal_emulator) for [Cygwin](http://www.cygwin.com) and [MSYS](http://www.mingw.org/wiki/MSYS). In Cygwin, it is installed as the default terminal by Cygwin's [setup.exe](http://www.cygwin.com/setup.exe). In MSYS, the mintty package can be installed with the command `mingw-get install mintty`. Alternatively, binaries for Cygwin 1.7, 1.5 and MSYS can be found on the [downloads](http://code.google.com/p/mintty/downloads) page.

Features include:

  * [Xterm](http://invisible-island.net/xterm)-compatible terminal emulation.
  * Native Windows user interface with a simple options dialog.
  * Easy copy & paste.
  * Drag & drop of text, files and folders.
  * Ability to open files and URLs with Ctrl+click.
  * Comprehensive character encoding support, including [UTF-8](http://www.utf-8.com).
  * Wide character display and Windows [IME](http://en.wikipedia.org/wiki/Input_method_editor) support.
  * Window transparency, including [glass effect](http://mintty.googlecode.com/svn/images/glass.jpg) on Vista and 7.
  * 256 colours.
  * Fullscreen mode.
  * Options stored in a text file. No registry entries.
  * Small program size and quick scrolling.

Mintty works on all Windows versions from Windows 2000 onwards. Similarly to other Cygwin/MSYS terminals based on [pseudo terminal](http://en.wikipedia.org/wiki/Pseudo_terminal) ("pty") devices, however, mintty is not a full replacement for the Windows Command Prompt. While native console programs with simple text output usually work fine, interactive programs often have [problems](http://code.google.com/p/mintty/issues/detail?id=56), although sometimes there are [workarounds](http://code.google.com/p/mintty/issues/detail?id=56#c12).

The Cygwin package ships with a manual page that can be accessed with _[man mintty](http://mintty.googlecode.com/svn/branches/1.1/docs/mintty.1.html)_. A PDF version of the manual is available from the [downloads](http://code.google.com/p/mintty/downloads) page. Invoking mintty with the _--help_ option shows a summary of available command line options. [Keycodes](Keycodes.md), [control sequences](CtrlSeqs.md) and some random [tips](Tips.md) can be found on the [wiki](http://code.google.com/p/mintty/w).

Please report bugs or suggest enhancements via the [issue tracker](http://code.google.com/p/mintty/issues). Vote for any issues you'd particularly like to see addressed by starring them. The discussion group for all things mintty is [mintty-discuss](http://groups.google.com/group/mintty-discuss). General Cygwin questions should be sent to the [Cygwin mailing list](mailto:cygwin@cygwin.org).

Mintty is based on code from [PuTTY](http://www.chiark.greenend.org.uk/~sgtatham/putty) [0.60](http://the.earth.li/~sgtatham/putty/0.60/putty-src.zip) by Simon Tatham and [team](http://www.chiark.greenend.org.uk/~sgtatham/putty/team.html). The program icon comes from [KDE's Konsole](http://konsole.kde.org). Mintty ties directly into Cygwin/MSYS and leaves out PuTTY's networking functionality, which is provided by packages such as [openssh](http://www.openssh.com) and [inetutils](http://www.gnu.org/software/inetutils) instead. A number of [PuTTY issues](PuttyIssues.md) have been addressed.

![Mintty help screen shot](http://mintty.googlecode.com/svn/images/screenshot.png)
