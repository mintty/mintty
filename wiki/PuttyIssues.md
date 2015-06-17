Mintty addresses the following PuTTY bugs and wishes from the list at http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist, some of them only partially. Many of the issues on that list regard PuTTY's networking functions, which of course no longer apply to mintty.

Bugs:
  * [assert-fail-newsavelines](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/assert-fail-newsavelines.html): Negative scrollback size settings are ignored.
  * [assert-line-not-null](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/assert-line-not-null.html): Screen lines are now stored in an array rather than a tree, and this issue hasn't been seen.
  * [change-scrollback-altscr](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/change-scrollback-altscr.html): The alternate screen is no longer cleared when the scrollback size is changed.
  * [ctrl-pgupdn-config](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/ctrl-pgupdn-config.html): Line-by-line scrolling is done with Shift+arrow up/down rather than misappropriating PgUp/Dn. The modifier is configurable, and the scrolling shortcuts can be disabled altogether.
  * [keyboard-problems](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/keyboard-problems.html): It looks like these are all gone, probably mostly due to fixes to the Windows keyboard layouts. IME handling seems fine too.
  * [resize](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/resize.html): Resize handling has been much simplified and seems fairly solid now. Font resizing has been split off into a separate feature (using Ctrl+plus/minus/zero or Ctrl+mousewheel).
  * [startup-unselected](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/startup-unselected.html): Startup handling has been redesigned and mintty behaves correctly when invoked e.g. with `cygstart --showna`.
  * [systray-breaks-ptr-hiding](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/systray-breaks-ptr-hiding.html): Looks like this had been fixed in PuTTY 0.60 already.
  * [win-tile](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/win-tile.html): Mintty behaves correctly when told to resize by Windows itself or by 3rd party window managers. The window is sized exactly as requested, with padding inserted at the bottom and right edges if necessary. This plays nicely with Windows 7's new window management features such as the Win+arrow shortcuts.
  * [window-title-chars](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/window-title-charset.html): Title-setting escape sequences are interpreted according to the currently selected charset.

Semi-bugs:
  * [da-response](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/da-response.html): Mintty sends the same response to a DA (device attribute) request as xterm does: `\e[?1;2c`.
  * [dbcs-breakage](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/dbcs-breakage.html): Double-byte charsets such as GBK or SJIS work fine.
  * [scroll-button-hang](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/scroll-button-hang.html): Mouse click and scrollwheel handling has been redesigned and this issue has not been seen with mintty.
  * [sgr21](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/sgr21.html): The SGR 21 sequence does the same as in the Linux console, i.e. selecting normal text intensity.
  * [window-placement](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/window-placement.html): Initial window placement follows default Windows behaviour (by using AdjustWindowRect and GetWindowDC(0) as suggested).

"Fun" wishes:
  * [autoscroll-accel](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/autoscroll-accel.html): Scrolling speed during selections is proportional to how far the mouse pointer is outside the window.
  * [bold-font-colour](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/bold-font-colour.html): Mintty has separate options for showing the ANSI bold attribute with a thicker font or brighter colour, allowing both to be enabled at the same time.
  * [cmdline-licence](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/cmdline-licence.html): `mintty --version` prints a brief copyright and licence statement.
  * [startup-fullscreen](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/startup-fullscreen.html): The `--window=full` command line option (or -wf for short) allows to start mintty in fullscreen mode. That's passed on to additional sessions opened with Alt+F2.

"Tricky" wishes:
  * [cmdline-any-option](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/cmdline-any-option.html): All config-file settings can be specified on the command line via the -o option.
  * [config-inheritance](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/config-inheritance.html): This is sort-of implemented through the ability to specify multiple config files on the command line.
  * [config-locations](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/config-locations.html): Mintty stores its configuration in a file rather than the registry.
  * [right-alt](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/right-alt.html): The right Alt/AltGr key acts as AltGr when the keyboard layout assigns a character to the key combination being pressed. Otherwise, it acts as a second Alt key.
  * [unicode-mappings](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/unicode-mappings.html): The Unicode hyphen character (U+2010) is mapped to ASCII hyphen/minus on display. This should probably depend on whether the glyph is really missing from the font though (e.g. using GetGlyphIndices).

"Fun" unsure wishes:
  * [env-passthrough](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/env-passthrough.html): All environment variables are passed through to the child process, except that TERM and possibly LANG are set. This isn't optional though.
  * [paste-semantics](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/paste-semantics.html): Copy-on-select can be disabled. A context menu entry and the Ctrl+Ins shortcut allow explicit copying. The right click behaviour is configurable, but shows the menu by default. Middle click pastes.
  * [resize-no-truncate](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/resize-no-truncate.html): Narrowing the window does not throw away excess line content, i.e. it reappears if the window is widened again afterwards.
  * [triple-click-wrap](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/triple-click-wrap.html): Triple click selects lines wrapped over multiple screen lines. Convenient for those monster compile lines.
  * [utf8-plus-vt100](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/utf8-plus-vt100.html): VT100 line drawing mode works in UTF-8 mode, as it does in xterm.
  * [xterm-keyboard](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/xterm-keyboard.html): Keycodes are compatible with xterm in its default "PC-style" function key mode, including support for encoding modifier keys. VT220 mode is supported too.

"Tricky" unsure wishes:
  * [drag-drop](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/drag-drop.html): Text, URLs, and files can be dropped into the mintty window. Files are inserted as POSIX paths.
  * [multiple-connections](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/multiple-connections.html): PuTTY's version of [issue 8](http://code.google.com/p/mintty/issues/detail?id=8): tabs. Mintty doesn't have those either, but it has the Ctrl+Tab shortcut for switching among mintty windows.
  * [scrollbar-left](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/scrollbar-left.html): The scrollbar can be put on the left-hand side. This seems to work on all Windows versions since at least XP, inspite of what MSDN says.
  * [selection-pause](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/selection-pause.html): Output is correctly paused and buffered during a selection.
  * [unicode-normalisation](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/unicode-normalisation.html): Where possible, combining characters are folded with their cell's base character on arrival. This relies on Windows support. Precomposed glyphs usually look a lot better than ad-hoc combinations.
  * [url-launching](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/url-launching.html): URLs and also local files can be opened by clicking on them while holding down Ctrl, or by selecting them and choosing Open from the context menu.
  * [windows-utf16](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/windows-utf16.html): Mintty internally uses Windows' native UTF-16 encoding, and surrogate pairs are handled correctly. This means that characters beyond the Basic Multilingual Plane (BMP) are fully supported in screen output, keyboard input and clipboard operations.

"Taxing" unsure wishes:
  * [function-keys](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/function-keys.html): PuTTY's various options for function key codes and Home/End are gone. Instead, mintty concentrates on emulating Thomas Dickey's latest xterm keycodes correctly, and that seems fairly successful in avoiding keycode issues.
  * [modified-fkeys](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/modified-fkeys.html): Mintty implements xterm's default scheme for encoding modified function, arrow and editing keys.
  * [win-command-prompt](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/win-command-prompt.html): Acting as a local terminal of course is the whole point of mintty. It's intended primarily for Cygwin programs, including network clients such as ssh, rsh, or telnet. Native Windows command line programs can be run too, but as pointed out in the PuTTY wish, there are problems with that. See [issue 56](http://code.google.com/p/mintty/issues/detail?id=56) for more on the latter.

Non-wishes:
  * [control-window-pos](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/control-window-pos.html): The initial window position can be set with the --position (or -p) command line option.
  * [transparency](http://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/transparency.html): Both simple Win2k-style transparency and Vista-style glass transparency are available.