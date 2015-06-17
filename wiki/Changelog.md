### 1.1.3 (7 April 2013) ###
  * Tweaked cursor colour handling to avoid the cursor becoming invisible under certain circumstances.
  * Fixed incorrect response to OSC sequence for querying colour settings.
  * Page-by-page scrolling now scrolls by one line less than a whole page.
  * Added 'AppID' setting for overriding the default taskbar grouping on Windows 7 and above. (Note that the AppID of a pinned shortcut is separate from that of the program pointed to. It can be set with the free [win7appid](http://code.google.com/p/win7appid) utility.)

### 1.1.2 (13 September 2012) ###
  * Fixed buffer overflow in processing of the control sequence for querying font coverage.
  * Tweaked default double-click word selection algorithm to handle shell variable references (_$FOO_ and _%FOO%_) and also exclamation marks in URLs.
  * Changed clicks-place-cursor feature to respect application cursor key mode when sending left/right arrow keycodes.

### 1.1.1 (16 June 2012) ###
Colours:
  * Turned up brightness of ANSI bold blue by a couple of notches.
  * Allow colours in settings to be specified in X-style hexadecimal formats such as #RRGGBB or rgb:RR/GG/BB (rather than just comma-separated decimal).
  * Re-rendered the program icon in true colour rather than 256 colours, with proper alpha channel.
  * Fixed a bug where the background erase colour wasn't being updated when a saved cursor was restored, which caused problems with custom vim colour schemes.

Mouse:
  * Implemented xterm's private mode 1006 for encoding mouse events, which unlike the other three existing encodings includes the button number in button release events.
  * Fixed off-by-one error in encoding of mouse coordinates in urxvt's private mode 1015.
  * Tweaked mouse handling to make it easier to select a single character.
  * When the right click action is set to extend the selection, a left click with Shift pressed now pastes the clipboard instead of extending the selection, as in rxvt.

Compatibility:
  * Improved handling of VK\_PACKET messages, which tools like AutoHotKey or AllChars use to send characters, so as to accept characters outside the system's ANSI codepage.
  * Tweaked RTF clipboard output to make it work in Lotus Notes.
  * Fixed some xterm incompatibilities regarding saved cursors and the alternate screen: origin and autowrap mode should be saved with the cursor, and the alternate screen shouldn't have its own cursor, scroll margins or insert mode.

Misc:
  * Log output can now be sent to standard output by specifying a dash instead of a filename.
  * Fixed rough display of some bold fonts, e.g. Consolas.
  * Stopped selection from moving when the active screen is scrolling while looking at the other screen (via the Flip Screen command).

Build:
  * Added manifest spell to keep the Program Compatibility Assistant away.
  * Changed compiler options to build for i686 rather i586, and optimise for speed rather than size.
  * On Cygwin 1.7, mintty is now built using gcc 4.5 rather than 3.4, and tuned for the Atom processor (because Atom's in-order architecture should have most to gain from this).

### 1.0.3 (30 December 2011) ###
  * Fixed crash when trying to insert or erase out-of-range lines.
  * Implemented xterm title stack feature (limited to 16 entries).
  * Failure to load a custom program icon now triggers a warning rather than a fatal error.

### 1.0.2 (19 November 2011) ###
  * Fixed a bug that caused attributes for the wrong cells to be picked up when copying characters on systems with a double-byte default codepage.
  * Added missing O\_TRUNC flag when opening a log file.
  * Take bold attribute into account when determining font coverage via the OSC 7771 sequence.
  * Ignore title request sequence instead of sending an empty reply.
  * On Cygwin 1.7, stop setting LANG to the system default locale (currently "C.UTF-8") if the locale isn't set either in the environment or in the options.
  * Reflect Cygwin 1.7.10 name change of pty master devices from "/dev/ttyN" to "/dev/ptyN" when creating utmp entries.

### 1.0.1 (24 July 2011) ###
  * Fixed a bug in the creation of utmp entries that limited the number of mintty entries to 10. Also prepared for the possibility of Cygwin switching to the Unix98 pty naming scheme.
  * Included semicolon in word selection, for the sake of URLs using them to separate parameters.
  * The cursor now is updated immediately when its shape is changed using the DECSCUSR control sequence.

### 0.9.9 (12 June 2011) ###
  * The transparency level can now be set to any value ranging from 4 to 254 in the config file or in the command line. (For backward compatibility, values below 4 are multiplied by 16. 255 is "Glass".)
  * Implemented mintty-specific OSC sequence 7771 for checking which characters are available in the currently selected font.
  * Ctrl+symbol and Ctrl+number combinations that yield control characters are no longer overridden in xterm "modifyOtherKeys" mode level 1, but only level 2.
  * Stopped sending bogus control sequences for some Ctrl+symbol combinations. Also stopped trying to add Shift to Ctrl+symbol combinations to obtain a control character. That's only done for Ctrl+number combinations now.
  * Moved implementation of private mode 30 for hiding and showing the scrollbar (as introduced by rxvt) to the mintty-specific private mode 7766, because it doesn't do quite the same thing: instead of changing window size to account for the width of the scrollbar, mintty changes the number of character columns.
  * Stopped rogue mouse release events being sent when application mouse mode is overridden with Shift.
  * Middle button paste now happens on release rather than click of the button, as in xterm and rxvt.

### 0.9.8 (21 May 2011) ###
  * Fixed a security issue with the OSC 701 sequence for setting and querying the terminal's locale, which allowed an arbitrary string (not including line endings) to be set as the locale and then echoed back as if typed by the user. Querying the locale now will only return valid locale strings.
  * Dropped the mintty-specific OSC 7776 sequence, which did the same as OSC 701 (as introduced by rxvt-unicode).
  * Implemented rxvt-unicode's private mode 1015 for encoding mouse events, which unlike standard xterm mouse reporting uses properly formed CSI sequences and which allows for unlimited mouse coordinates.
  * Implemented missing xterm control sequences for window operations: set pixel size, get screen size in characters, refresh window, full screen.
  * Implemented selective erase, where the DECSCA sequence can be used to set a protection attribute on characters, which are then left alone by the DECSED (selective erase in display) and DECSEL (selective erase in line) operations.
  * Implemented DECRQSS sequences for requesting various parameters including current text attributes and scroll margins.
  * Tightened up parsing of control sequences and brought it more into line with xterm. Control characters within ESC and CSI sequences now get processed. Missing CSI parameters are treated as zero, whereas extraneous parameters are ignored. APC and PM string sequences now get parsed and ignored.
  * If an AltGr key combination yields a character that can't be encoded in the current charset, the AltGr is treated as Alt instead.
  * `true` and `false` are now recognised as values for Boolean configuration settings, in addition to `yes` and `no`.

### 0.9.7 (17 Apr 2011) ###
  * Fixed crash when scrollback size is set to zero.
  * Fixed support for codepoints beyond the Basic Multilingual Plane (BMP) in Cygwin 1.5 and MSYS.
  * NT4 support is officially gone. It had been broken since version 0.6.1 anyway, without anyone complaining.
  * When the window is held open after the shell finished and no more processes are attached, hitting Enter or Escape will now close the window.
  * Changed the optional Ctrl+Shift shortcut for closing the window from Ctrl+Shift+Q to the more standard-compliant Ctrl+Shift+W.
  * Added config file versions of command line options: Class, Hold, Icon, Log, Title, Utmp, Window, X, Y.
  * Multiple-choice settings are now written with named values rather than integers, e.g. `CursorType=block` instead of the rather non-obvious `CursorType=0`. Similarly, Boolean settings are written as `yes` or `no` instead of `0` or `1`. Integer values of course remain supported for backward compatibility.
  * String settings no longer have arbitrary length restrictions.
  * Unknown settings and invalid values now trigger warnings.

### 0.9.6 (20 Feb 2011) ###
  * Fixed crash triggered by lots of combining characters on the same line.
  * Corrected initialisation of the bold background colour. (This is used to display the blink attribute when blinking is disabled.)
  * Added _Show bold as font_ option. Previously, disabling _Show bold as colour_ would cause text with the bold attribute to be displayed with a thicker font instead of a brighter colour. Having these as separate options allows bold text to be shown with both a thicker font and a brighter colour, which is the default in xterm and others. The mintty default remains bold-as-colour-only.
  * Added options that allow to disable the copy and paste shortcuts Ctrl+Ins and Shift+Ins as well as the various Alt+Fn window command shortcuts.
  * Added an option for enabling a set of Ctrl+Shift+letter shortcuts as found in KDE Konsole and GNOME Terminal, as an alternative to the Ctrl/Shift+Ins and Alt+Fn shortcuts. Examples include Ctrl+Shift+V for paste and Ctrl+Shift+N for opening a new window. (These only appear in menus if the corresponding default shortcuts are disabled.)
  * Tweaked option wording and placement (again). The scrolling option now are all on the Window pane of the options dialog.

### 0.9.5 (28 Dec 2010) ###
  * Enabled ECHOCTL, ECHOE, ECHOK, ECHOKE, and IMAXBEL terminal line flags at startup, for compatibility with other terminal emulators and `stty sane`.
  * Worked around occasional failure to deliver SIGCHLD by redesigning exit handling yet again.
  * When keeping the window open after the command is finished, the cursor is switched off and the window has to be closed with the Close button or Alt+F4, rather than just any key.
  * With `--hold=error`, the check for runtime error signals was the wrong way round.
  * Worked around failure of 'bbLean' window manager to send WM\_EXITSIZEMOVE when finishing a resize.
  * When the locale is set in the options, all LC`_``*` environent variables rather than just LC\_ALL and LC\_CTYPE are now cleared. (LANG is set according to the selected locale.)
  * Added hostname to utmp entry.
  * Fixed a bug confusing NUL characters with wide characters.
  * Fixed a bug that caused a failure to invoke the user's shell in debug builds.

### 0.9.4 (18 Dec 2010) ###
  * Reinstated flow control with `^S` and `^Q`, following protests. Hence the stty command to enable it in 0.9.3 is no longer needed. `stty -ixon` can be used to disable it and make `^S` and `^Q` available for other functions again.
  * The `ixany` terminal flag is set instead, which tells the terminal driver to restart output on any input character rather than just `^Q`, thus still stopping users from "hanging" their terminal.
  * Tweaked the layout and wording of some options. Renamed the 'Output' pane to 'Terminal'.

### 0.9.3 (4 Dec 2010) ###
  * Disabled flow control with `^S` and `^Q` by default, to stop unwary users from accidentally "hanging" their terminal and to make those keys available for other functions. To re-enable, use `stty stop ^S start ^Q`.
  * Stopped resizing the window when changing font size with Ctrl+plus/minus/zero or Ctrl+mousewheel. This follows the example of KDE's Konsole and is more consistent with other programs that have this feature.
  * Changed interpretation of the `--hold=never` command line option (or `-hn` for short) such that the window should really never stay open after the shell process finishes. The default remains to leave the window open when the shell process exits with status 255. This is used to signal failure to execute the shell command, and it is also used by ssh to indicate connection failure.
  * Tweaked SIGCHLD handling to reap all dead processes that come mintty's way, not just the shell process.
  * Fixed a memory corruption bug in the handling of combining characters.
  * Fixed crash when trying to open a filename that cannot be converted to Windows format.

### 0.9.2 (26 Oct 2010) ###
  * Implemented hexadecimal [Alt codes](http://en.wikipedia.org/wiki/Alt_code), as per MS's interpretation of ISO 14755.
  * Fixed a bug with Alt codes that caused them not to work if the first digit was not entered quickly after holding down Alt.
  * Reinstated support for opening relative paths in child processes with Ctrl+click or the 'Open' command. (Thanks to Christopher Faylor for changing tcgetpgrp() to allow this.)
  * Avoided a crash that somehow was triggered by Cygwin's setup.exe running postinstall scripts under some circumstances.

### 0.9.1 (3 Oct 2010) ###
Documentation:
  * Turned some man page sections into wiki pages (at http://code.google.com/p/mintty/w): keycodes, control sequences, and tips.
  * Added a tip on how to use Ctrl+Tab and Ctrl+Shift+Tab to switch session in GNU screen.
  * Added a wiki page with past changes and one listing PuTTY issues that are addressed in mintty.

Display issues:
  * On multimonitor systems, the window size is no longer limited to the size of a single monitor.
  * The program window should no longer be opened with parts off the screen or obscured by the taskbar (unless of course the window is too big to fit into the available workspace).
  * Fixed an issue with cursor flicker on Vista and 7 with Aero disabled.
  * The options dialog no longer flashes when changing page while transparency is enabled, as happened on non-Aero systems.
  * Added automatic fallback scheme for VT100 line drawing characters. If appropriate Unicode characters aren't available in the selected font, ASCII approximations are used instead.

Colours:
  * Added ability to set the 16 ANSI colours in the config file (or on the command line via the -o option), like so: `Blue=0,0,255` or `BoldGreen=128,255,128`. The manual has all the colour names.
  * Added ability to switch cursor colour depending on whether the Input Method Editor (IME) is active. This is activated by setting _IMECursorColour_ in the config file (or via the -o option). So, for example, adding `IMECursorColour=255,0,0` to _~/.minttyrc_ will turn the cursor red when the IME is active. (IMEs allow entering characters that aren't on the keyboard and are crucial for East Asian languages.)
  * Renamed _Show bold is bright_ setting to _Show bold as colour_.
  * Removed the _Use system colours instead_ checkbox from the options dialog. The _UseSystemColours_ config file setting remains.

Selection:
  * Added config-file only _WordChars_ setting for controlling the characters selected by a double click. By default, mintty uses an algorithm that's geared towards picking out filenames and URLs. If WordChars is set, that algorithm is disabled, and instead only letters, digits, and the characters specified with this setting are selected. For example, setting `WordChars=_` would ensure that C identifiers are picked out correctly.
  * Fixed a crash that occurred when copying lots of text on systems with a doublebyte default codepage.

Xterm compatibility:
  * Added support for xterm's VT220-style function key mode (as opposed to the default "PC-style" keycodes), where Ctrl+F3 through Ctrl+F10 act as F13 through F20, the Home and End keys send different keycodes, and the numpad sends "application keypad" codes if enabled with the DECPAM sequence.
  * In mouse tracking mode, concurrent mouse button presses are now handled in the same way as they are in xterm, i.e. mintty no longer sends a fake mouse release event when the second button is pressed.
  * 'Extended Mouse Mode' as introduced in xterm #262 is now supported. This allows row/column positions greater than 255 (and up to 2015) to be reported, in case you do get that 30'' monitor ...
  * Normalise incoming combining characters to the precomposed from, as xterm does. This makes them look better on screen.

Misc:
  * Fixed crash in Ctrl+Tab handling that showed up on some setups.
  * Handle VK\_PACKET virtual key, to ensure compatibility with the _AllChars_ utility for emulating a Compose key on Windows.
  * Alt+F4 prompts for exit confirmation if the shell has any child processes, as already happens with the close button. (There's an option for disabling this.)
  * Changed the SGR 21 sequence from setting the underline attribute to selecting normal intensity, for compatibility with the Linux console. (Xterm ignores this one.)

### 0.8.3 (5 Sep 2010) ###
  * Guard against Windows DLL hijacking vulnerability.
  * Fixed a bug that occasionally caused the end of the child process to be missed and hence the terminal window to wrongly stay open.
  * With --hold=all or --hold=error, make sure the process termination status is only printed after the last output from the pty.

### 0.8.2 (23 Aug 2010) ###
  * Fixed bug causing 100% CPU consumption when keeping the terminal open after the child process exited.
  * The next keypress after the child process exited closes the terminal.
  * Fixed bug that opened the window in fullscreen mode when it was meant to be hidden (e.g. when started with cygstart's --hide argument).
  * Tweaked postinstall and preremove scripts to better cope with "Just for me" installations.

### 0.8.1 (6 Aug 2010) ###
  * Ported mintty to MSYS (i.e. Cygwin 1.3).
  * Copy-on-select is enabled by default.
  * The default font size was reduced from 10 to 9, more in line with the Windows console's default.
  * I/O handling was redesigned using /dev/windows and select(). The result of this is that signals sent to mintty are now handled immediately. In particular, suspending mintty works properly now.
  * A new option allows to put the scrollbar on the left-hand side of the window. It's on the 'Window' pane of the options dialog.
  * The line cursor is displayed with the thickness configured in the Windows accessibility control panel (which defaults to 1 pixel).
  * Excess line content is no longer thrown away when narrowing the window, i.e. it now reappears if the window is widened again afterwards. (Rebreaking of long lines is not done though.)
  * Changing the font smoothing setting takes effect immediately after pressing OK or Apply.
  * Failure to save options triggers an error message instead of silently forgetting the options.
  * A couple of annoyances in the handling of mouse clicks were fixed.

### 0.7.1 (18 Jun 2010) ###
Bug fixes:
  * New mintty sessions created with Alt+F2 no longer needlessly inherit file handles from their parent, which stopped pty devices from being reused.
  * Fixed a bug that stopped output from working after suspending and resuming mintty.
  * Avoid the cursor becoming invisible, by changing cursor colour if it's too close to the text background colour.
  * Avoid innocent characters being swallowed after encountering an incomplete 4-byte UTF-8 sequence.

Windows and screens:
  * Added Ctrl+Tab and Ctrl+Shift+Tab shortcuts for switching between mintty windows. These are controlled by a new checkbox on the Keys page of the options dialog, which is enabled by default. Disable to send the keycodes `\e[1;5I` and `\e[1;6I` to the application again.
  * Added --window=normal|min|max|full command line option for setting the initial window state.
  * Added 'Flip Screen' context menu command (with Alt(Gr)+F12 shortcut) for looking at the alternate screen while on the primary screen and vice versa. This allows to peek at the last man page viewed while on the command line, or to copy from the command line while editing a file.

Scrolling:
  * Removed 'Access scrollback from alternate screeen' option. This always suffered from confusion between scrollback and application scrolling. 'Flip Screen' makes it unnecessary.
  * Added 'Page Up/Down scroll without modifier' option to Keys page of the options dialog. This allows accessing the scrollback without pressing Shift or another modifier. Hold that modifier to send Page Up/Down to the application anyway. The option does not affect arrow up/down or Home/End, i.e. the modifier will still need to be pressed to scroll line-by-line or go to the start or end of the scrollback. It also does not affect alternate screen applications, i.e. Page Up/Down will continue to scroll the file in the likes of less or vim.
  * Added xterm/rxvt control sequence for hiding (`\e[?30l`) or showing (`\e[?30h`) the scrollbar. It always remains hidden if 'Show scrollbar' on the 'Window' page of the options is disabled.

Copy & paste:
  * Pasting text is quite a lot faster, so that pasting hundreds of lines should now be tolerable. Still wouldn't recommend pasting thousands of lines.
  * If the right click action is set to paste, middle-click now extends the selection rather than paste as well.
  * Added "Copy Title" command to the window menu (which can be accessed by clicking on the window icon, right clicking on the titlebar, or pressing Alt+Space). This copies the window title to the clipboard.
  * Added 'Copy as rich text' option, which is on by default. Mintty always copied text both as plain text and as rich text, whereby the latter allows pasting with colours and formatting into applications that support it. Sometimes that's not what's wanted, hence the option.
  * Don't clear the selection when another program copies something to the clipboard.
  * In word selection, don't include dollar and percent signs at the start or end of a word. Do include plus and minus.
  * Added xterm bracketed paste mode, which allows programs to differentiate between keyboard input and pasted text. See http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#Bracketed%20Paste%20Mode.

Input:
  * Added mintty-specific control sequence for enabling (`\e[7786h`) or disabling (`\e[7786l`) mousewheel reporting on the alternate screen. This is what allows mousewheel scrolling in 'less' and others. It's enabled by default.
  * Removed 'Lone Alt sends ESC' option. This seemed a good idea at the time, but I've got no evidence that anyone actually used it. Worse, it sent an inadvertent ESC when using Alt+Tab for switching windows, and there was no way to fix that.

Other:
  * Set log file permissions to read/write for the user only.
  * Improved rendering of combining characters by letting Windows do the combining rather than just printing the characters on top of each other.
  * Removed terminal-side line editing, which apparently was a VT131 feature. Can't imagine anyone used it, especially as Xterm doesn't support it, so it needlessly complicated input processing.
  * Lots of internal changes aimed at improving speed and maintainability.

### 0.6.2 (21 Apr 2010) ###
  * Tweaked double-click selection and Ctrl-click opening to recognise URLs with parameters.
  * Added ability to kill the child process outright using SIGKILL instead of SIGHUP by holding down Shift while using the close button, Alt+F4 shortcut, or menu item.
  * Support maximized startup on Windows 7.
  * Fixed issue when copying text that caused black blocks to appear at the end of lines when pasting into some applications, e.g. Wordpad.
  * Fixed an issue with the left Ctrl key not being recognised when it was pressed while another window was active.
  * Keep the mintty window open showing an error message if invoking the shell or specified command fails.
  * Changed fallback shell back to /bin/sh.
  * Save both the G0 and G1 charset settings when saving the cursor or switching to the alternate screen.

### 0.6.1 (26 Mar 2010) ###
User interface:
  * Dropping or pasting files into the mintty window now inserts Cygwin paths rather than Windows paths. Paths with characters that are special to shells are quoted appropriately.
  * Error messages and help output are now displayed in a message box if writing to stdout or stderr fails, e.g. if mintty is invoked from a console or a shortcut.
  * The 'Duplicate' window command was renamed to 'New', because 'Duplicate' might have raised expectations that the whole session including the state of all the processes inside it is duplicated, which isn't really possible. The Alt+F2 keyboard shortcut remains.
  * When pasting from the clipboard, convert Unix line endings, i.e. linefeeds (`^J`), to carriage returns (`^M`), because that's what the Enter key sends. (Sending `^J` caused trouble in nano, where that's the 'Justify' command.)

Config-file handling:
  * Options can be read from multiple config files by providing multiple --config switches. If an option is set in multiple files, the last one wins.
  * /etc/minttyrc is read before ~/.minttyrc, to allow the administrator to specify system-wide options.
  * Options are stored to the last config file specified, or ~/.minttyrc otherwise. Only options that have been changed in the dialog are added to the file.
  * Config-file options can now be specified directly on the command line using the -o/--option switch. For example: -o Font=Consolas

Text display:
  * Bold glyphs should no longer get clipped on the left edge, although depending on the size of the glyphs they might now get clipped on the right.
  * The ColSpacing and RowSpacing allow pixels to be added or removed between characters. Currently, these don't appear in the options dialog, i.e. they have to be specified in a config file or on the command line. ColSpacing=1 can avoid the problem with bold glyphs being clipped, whereas RowSpacing=-1 works nicely with the Consolas fonts, which normally leaves a bit too much space between lines. (Thanks to Paul Martin for the idea.)

Keyboard:
  * Ctrl+AltGr combinations now work as they do in X11, for example, Ctrl+AltGr+Q on a German keyboard yields `^@`, because AltGr+Q yields @. (Thanks to Thomas Wolff for his help with this knotty issue.)
  * The AltGr key on non-US keyboards acts as a second Alt key in any key combinations that the Windows keyboard layout does not have a keycode for. This can be more convenient than the left Alt key.
  * Ctrl+Shift combinations now send control characters in the C1 range (U+0080..U+009F), encoded in the selected charset. For example Ctrl+Shift+A will send 0x81 in any of the ISO charsets, and 0x92 followed by 0x81 in UTF-8. Where C1 control chars aren't supported, Ctrl+Shift combinations continue to send the same code as Ctrl+Alt combinations (i.e., ESC followed by `^A` in the case of Ctrl+Shift+A).

Locales and charsets:
  * The options dialog no longer tries to second-guess the user's locale and charset field input.
  * Support for the Chinese GB18030 charset was added. Cygwin doesn't (yet) support it though, which is why it doesn't appear in the charset dropdown.
  * The terminal's locale can be changed using urxvt's OSC 701 control sequence, for example `\e]701;ja_JP.SJIS\a`. An empty sequence will restore the original locale: `\e]701;\a`. The previously introduced mintty-specific sequence for this has been deprecated.

Startup & exit:
  * Fall back to /bin/bash rather than /bin/sh if no shell is specified otherwise. This is for the benefit of Windows domain account users, for whom Cygwin setup does not create /etc/passwd entries by default.
  * Ignore SIGHUP, which means that if mintty is started from a terminal, closing that terminal won't close mintty. That's what happened already when mintty was invoked from a console.
  * When trying to close the window, follow the xterm approach of only ever sending SIGHUP rather than escalating to SIGTERM. This leaves it up to the application running in mintty whether to really close.

Misc:
  * Implemented xterm control sequence for allowing or disallowing 80/132 column mode switching. It's disallowed by default, which cures an annoying resize when invoking screen with TERM=xterm-256color.
  * Fixed crash on Output page of options dialog that occurred when the Windows printer spooler service was disabled.
  * Support for setting different terminal compatibility levels is gone.

### 0.5.8 (27 Feb 2010) ###
  * Font names with non-ASCII characters weren't read correctly from the config file, thus causing settings such as ＭＳ ゴシック (MS Gothic) to be ignored.

### 0.5.7 (26 Jan 2010) ###
  * Ctrl+slash now sends `^_` as in xterm.
  * New --class command line option allows to change the name of mintty's window class. That can make it easier to distinguish different mintty windows in scripting tools like AutoHotKey.
  * Dim mode is no longer limited to the 16 basic colours.
  * Drop long path prefix ('\\?\') from Windows paths when opening files or directories. Some programs couldn't deal with those properly.
  * Bring the charset support into line with upcoming changes in Cygwin 1.7.2: use nl\_langinfo(CODESET) instead of making assumptions about locales without explicit charsets, and allow three-letter language codes.
  * The postinstall and preremove scripts should now work in non-admin setups.

### 0.5.6 (29 Dec 2009) ###
  * Fixed handling of right Alt key on US keyboards, which was broken in 0.5.5.
  * Changed the dim text attribute implementation to blend foreground and background colour, but only when the "Show bold as bright" option is off. This yields better results with black-on-white colour schemes.
  * Added support for decimal colour values in colour setting sequences, e.g. `\e]10;255,255,0\a`.
  * When Ctrl is pressed together with a digit or symbol key, check whether Shift+key yields a character corresponding to a control char (after first trying the plain char followed by AltGr+key). For example, Ctrl+6 on a US keyboard will now send `^^` (0x1E), because Shift+6 is `^`. This is for compatibility with the console.

### 0.5.5 (22 Dec 2009) ###
  * Extended the --icon option to be able to load the program icon from executables and DLLs as well as .ICO files. An icon index can also be specified, e.g. '--icon C:/Windows/explorer.exe,6'.
  * Added 'Use system colours' checkbox for overriding the colour settings with the standard Windows text and background colours (usually black-on-white, as seen e.g. in notepad).
  * The 'Opaque when focused' setting now applies in glass transparency mode too.
  * Implemented dim and invisible text attributes as specified in ECMA-48. These are selected with the `\e[2m` and `\e[8m` sequences.
  * Fixed the 'Ctrl+LeftAlt is AltGr' setting, which was applied the wrong way round.

### 0.5.4 (21 Nov 2009) ###
  * Reintroduced options for setting the TERM variable and `^E` answerback. (They're on the 'Output' page of the options dialog.)
  * The Low/Medium/High transparency settings were all the same.
  * With "clicks place cursor" enabled, double-clicking on a word not on the current line would wrongly move the cursor.

### 0.5.3 (7 Nov 2009) ###
  * The "clicks place cursor" feature now works correctly for double-width characters.
  * Fixed some weirdness in mouse selection where the newline character preceding a line would annoyingly be included in the selection.
  * Extended the xterm sequence for maximising and restoring the window: `\e[9;2t` will make it go fullscreen.
  * Added a mintty-specific control sequence for querying or changing font size, e.g. `\e]7770;+1\a` will increase font size by 1. The control sequences section of the manual has further details.
  * Added a mintty-specific control sequence for querying or changing locale and charset on the fly, e.g.: `\e]7776;C.ISO-8859-7\a` to switch to the Greek ISO charset. Again, further details in the manual.
  * Finally, some silly eyecandy: transparency can now be set to "Glass" on Vista and above with desktop compositing enabled. It turns the whole mintty window into a seamless sheet of "Aero glass". To make this anywhere near usable, the glass colour needs to be set to be as dark as possible in the Windows control panel: choose 'Personalize' from the desktop context menu, click on 'Window Color', turn the color intensity up to the maximum, show the color mixer, and turn the brightness down to black.

### 0.5.2 (24 Oct 2009) ###
  * Triple-click line selection now supports wrapped lines.
  * The newline character at the end of a line is no longer included when triple-clicking, to allow the line to be edited after pasting it back into the terminal.
  * Reintroduced an option for setting the backspace keycode back to `^H`.
  * Dropped the option for disabling the Copy&Paste shortcuts.
  * Added 'shortcut override mode', enabled with `\e[?7783h` and disabled with `\e[?7783l`. When this is on, all mintty shortcuts are overridden and sent to the application instead.
  * Improved window resizing behaviour.

### 0.5.1 (12 Oct 2009) ###
  * Renamed project from "MinTTY" to "mintty".

Keyboard:
  * The default backspace keycode now is `^?` rather than `^H`, to make Ctrl+H available as a shortcut for other purposes, particularly as the help key in emacs. The backspace keycode option is gone. Instead, the DECBKM control sequence for changing the backspace keycode is now supported.
  * The escape keycode option was replaced with a mintty-specific control sequence: `\e[?7728h` to switch to `^\`, and `\e[?7728l` for standard `^[`.
  * Added 'Ctrl+Alt is AltGr' option. This is on by default, for standard Windows behaviour. If switched off, Ctrl+LeftAlt is treated separately from AltGr.
  * The backspace keycode change means that the default Ctrl+Backspace code also changes, from `^?` to `^_`. Any keybindings in .inputrc and elsewhere will need to be adjusted accordingly.

Scrollback:
  * Added an option to enable the command line scrollback when on the alternate screen (used by fullscreen apps such as editors). This option is off by default, in which case the mousewheel now sends standard cursor keycodes when on the alternate screen. Hence, mousewheel scrolling in 'less' should now work without special configuration.
  * The mintty-0.4 mousewheel keycodes can be enabled and disabled using the new control sequences  `\e[?7787h` and `\e[?7787l`. These can be used to distinguish the mousewheel from the cursor keys without enabling full mouse reporting.

Locales and charsets:
  * The 'Codepage' option is now called 'Character set', and there's a new 'Locale' option for language and territory.
  * If no locale is set in the options, mintty uses the locale specified via the environment variables LC\_ALL, LC\_CTYPE, or LANG.
  * If the locale option is set, the character set is appended to it and the LANG variable set accordingly and LC\_ALL and LC\_CTYPE are cleared.
  * e @cjknarrow locale modifier is automatically appended to LANG if an ambiguous-narrow font is used with an East Asian locale. (See also http://www.cygwin.com/1.7/cygwin-ug-net/setup-locale.html)
  * Any character sets supported by Cygwin or Windows can be used. The dropdown menu lists many of those supported by Cygwin, including UTF-8, the ISO charsets, and also the system's OEM and ANSI codepages. Other Windows codepages can be entered manually using the CP123 format. (Just entering the number works too.)
  * East Asian double-byte character sets such as GBK or eucJP are now supported (if they are installed).
  * Unicode characters outside the basic multilingual plane can now be displayed if a suitable font is available, which should always be the case on Vista and 7. (Please note, however, that currently many programs do not support these correctly, due to Windows' use of UTF-16 to represent Unicode).

Other:
  * Simplified configuration of the terminal bell. It's now possible to have it flash the screen and play the system sound at the same time. They're both off by default. Also, the option for continuous flashing of the taskbar is gone, so only the former "Steady" mode is available now. (The taskbar highlighting only happens if the bell is rung without the mintty window being active.)
  * Removed "SCOANSI" control sequences, which were already disabled by default.
  * mintty should now run on NT4, although with some limitations due to lack of features such as window transparency.
  * Rearranged options dialog.

### 0.4.4 (18 Jul 2009) ###
  * The "underline" cursor has become a slightly thicker, more visible "underscore".
  * The DECSNM reverse video mode now works as it does in xterm, swapping only the default foreground and background colours instead of swapping the colours of all character cells. (Enable with `\e[?5h`, disable with `\e[?5l`.)
  * The reverse attribute now works correctly in connection with the "Show bold as bright" setting. For example, it now turns bright red text on black into black on bright red instead of grey on red.
  * Added support for the 8-bit version of the 'rgb:' colour format implemented in 0.4.3. Also added missing string terminator to colour query reply.
  * Reset signal handlers in the child process like in xterm and rxvt. This addresses a problem with signals being ignored when mintty is invoked from a bash script.

### 0.4.3 (12 Jul 2009) ###
  * Removed Windows 7 console popup workaround for the next Cygwin 1.7 release (API version 0.211). No more subliminal console flashes, thanks to Christopher Faylor's better workaround in the Cygwin DLL.
  * When parsing the title sequence, the ST character (0x9C) was still being interpreted as string terminator, thereby causing occasional truncated UTF-8 or Windows codepage titles.
  * In mintty.exe's properties, the file version appeared as 0.0.0.0.
  * Added --icon option for customising the window icon.
  * Added support for 'rgb:RRRR/GGGG/BBBB' format in colour control sequences. Also added support for querying the current colour settings.
  * DECSCUSR control sequence for setting cursor style.
  * Introduced MinTTY-specific "application escape key" mode, where the escape key sends `\eO[` instead of just `\e`. This allows applications to avoid the escape timeout issue. (This is "experimental", meaning it might well change in future releases.)
  * Documented MinTTY-specific control sequences.
  * Added a tip to the manual on how to use DECSCUSR and application escape key mode to get a mode-dependent cursor in vim.

### 0.4.2 (28 Jun 2009) ###
  * Fixed a rather bad bug that meant that the ISO-8859-1 codepage was used for output in non-Unicode mode no matter the codepage setting.
  * Fixed erroneous NumLock detection, which broke numpad support in 'orpie'.
  * Fixed a problem with window resizing (again): after restoring from maximized state, part of the bottom line would disappear behind the window border.
  * The colour of text under a block cursor now is set to whichever of the foreground and background colours is further away (in colour space) from the cursor colour, to try to ensure legibility.
  * Dropped support for C1 control characters (i.e. 0x80 to 0x9F). This is a VT220 feature, whereas MinTTY only claims to be a VT100  via its "primary device attribute" string. Removing support makes Cygwin's /bin/ascii utility work correctly with any 8-bit codepage and decreases the likelihood of accidental binary output messing up the terminal settings. Rxvt doesn't support the C1 control characters either, but xterm does. Please let me know of any applications where this incompatibility causes problems.
  * Added a long version of the -e option ("--exec"), and documented them in the manual page and --help output.
  * Changed the man page tip on setting environment variables to use the 'env' command instead of 'sh -c'.
  * Added a tip on making bash and readline 8-bit clean, to allow non-ASCII input and output.

### 0.4.1 (26 Jun 2009) ###
  * The window title can now include characters from the full Unicode range rather than being limited to the system's ANSI codepage.
  * When resizing the window, the sizetip moves with the upper left corner of the window rather than being marooned at its original position.
  * Added one pixel of padding inside the window border to stop characters from touching it. (PuTTY had that, but I'd unwisely removed it.)
  * Alt+Numpad codes work when NumLock is off as well. In UTF-8 mode, Alt+1 to Alt+31 are interpreted as graphical OEM characters, e.g. Alt+3 is ♥ and Alt+1 is ☺.
  * Alt works as the Meta modifier for AltGr combinations, e.g. Alt+AltGr+7 on a German keyboard now sends `^[{` rather than just `{`.
  * Added Xterm window focus reporting. (`^[[?1004h` to enable, `^[[?1004l` disable. `^[[I` is sent for focus in and `^[[O` for focus out.)
  * Applications can ask to be notified when the width of the "CJK Ambiguous Width" characters changes due to the user changing font. `^[[?7700h` to enable, `^[[?7700l` to disable. <sup>[[1W is sent for cjknarrow mode, and `</sup>[[2W` for cjkwide mode. Removed attempted SIGWINCH notification for the same purpose, because it didn't work.
  * Bell overload detection wasn't working, but I decided to remove it instead of fixing it and to switch off the bell in the default config, because I'm guessing most people don't want to be beeped or flashed at by their terminal anyway.

### 0.4.0 (7 Jun 2009) ###
  * The options dialog gained an Apply button and no longer blocks terminal output. Options have been rearranged, hopefully for the better.
  * The row and column settings in the options now control the default size only, to avoid accidentally saving the current size.
  * Added a context menu command and the shortcut Alt+F10 for returning the terminal to its defaults size.
  * The official shortcut for the fullscreen mode now is Alt+F11. Alt+Enter continues to work, but might eventually be removed or used for something else.
  * The font size can be changed with Ctrl+plus, Ctrl+minus, or Ctrl+Mousewheel. Ctrl+zero goes back to the configured font size.
  * The command line cursor can be placed by clicking with the mouse, if the "Clicks place cursor" on the Mouse page of the options is enabled. This works by sending cursor key codes, but there are a few rough edges. In particular, when clicking on a command line that does not actually allow cursor movement, the cursor key codes are treated as input.
  * The command to run can now be set using the SHELL environment variable.
  * New --log command line option enables logging of terminal output to a file.
  * New --hold command line option controls whether to keep the window open after the command running in MinTTY has exited. This can be set to 'always', 'never', or 'error'. The default is 'never'; and 'error' enables the 0.3 behaviour.
  * PuTTY's option for setting the maximum number of lines in the scrollback buffer is back. (Previously that was fixed to 65535.)
  * The exit confirmation dialog when there are child processes can be disabled.
  * A utmp entry is created only if requested using the new --utmp option. (This is because utmp logging seems to often hang for about half a minute on Windows 7, for whatever reason.)
  * Decreasing the window size once again crops lines, because the attempted fix for this turned out to have a bug.
  * As is standard on Windows, AltGr now is always treated the same as Ctrl+Alt.
  * The control sequences sent by the mousewheel outside application mouse mode have changed. The previous scheme that tied the encoding to the "modifier for scrolling with cursor keys" was a bad idea, because it meant that the configuration of any program using it had to change depending on that setting.
  * MinTTY now has its own identity, instead of pretending to be an old xterm. The `^E` answerback string is "mintty", the `^[[c` primary device attribute command reports a vt100, and the `^[[>c` secondary DA command reports terminal type 77 (ASCII 'M') and version 400. The TERM variable remains set to "xterm", to avoid termcap/terminfo trouble.
  * Applications can get the whole numpad to send "application keypad mode" sequences by enabling "DECKPAM" and disabling "DECCKM". This diverges from xterm's behaviour in its default "PC-style function key" setting, but allows applications to tell numpad keys apart from their equivalents on the small keypads.
  * The (non-standard) keycodes for Ctrl combinations with digit and number keys have changed. They remain based on application keypad codes, but they now follow xterm's pattern for encoding modifier keys, e.g. Ctrl+1 is sent as `^[[1;5q`.
  * Xterm's "modifyOtherKeys" mode for encoding key combinations without standard keycodes is now supported, whereby the 'CSI u' format enabled by setting the "formatOtherKeys" resource to 1 in xterm is used.
  * The vt100 line drawing character set and the SCO/VGA graphical character set are now supported in Unicode mode.
  * xterm OSC sequences for changing text, cursor and other colours are supported.
  * A SIGWINCH signal is sent when a font with a different "CJK Ambiguous Width" is selected, so that applications can adjust their output accordingly.
  * The Windows keyboard layout is no longer consulted for Ctrl or Ctrl+Shift combinations, because non-English layouts often have control characters in unintuitive places. For example, on the German keyboard the `^^` character can now be obtained by pressing Ctrl+^ instead of Ctrl+Shift+6.

### 0.3.10 (29 Apr 2009) ###
  * The window title was left blank when non-Windows codepages such as the default ISO8859 ones were used.
  * Characters in the "CJK Ambiguous Width" category, e.g. Greek letters and line-drawing symbols, were displayed with the wrong width with several fonts on Windows 7.

### 0.3.9 (25 Apr 2009) ###
  * The console window that pops up on Windows 7 now gets hidden. Unfortunately the console still flickers up briefly, but the underlying problem has been reported to MS, so it might still get sorted before Cygwin 1.7 is released.
  * Characters in the "East Asian Ambiguous" category are now displayed as wide characters if a dual-width font such as MS Gothic or MS Mincho is selected. This is for compatibility with existing practice.
  * IMEs (Input Method Editors) are now supported.
  * UTF-8 and other codepages are supported for the window title.
  * The MinTTY window should no longer cling to the screen edges on startup.
  * External window resizes e.g. due to the taskbar's window arrangement commands are now supported.
  * Lines are no longer cropped when narrowing the window, i.e. hidden content reappears when widening it again.
  * The man page had a couple of typos in the .lesskey lines for activating mousewheel support in less.

### 0.3.8 (21 Mar 2009) ###
  * Added -e option for introducing the command to execute. This is for compatibility with other terminals, and makes mintty work with 'chere' again. (It worked in 0.3.5 only due to a bug in option handling.)
  * Added window menu command for duplicating the current session, with shortcut Alt+F2. This simply invokes mintty again with the same command line.
  * Added support for xterm control sequence to change the ANSI colours. (The Linux console's version of this was supported already.)

### 0.3.7 (16 Mar 2009) ###
  * The Home and End keys now send "PC-style" `^[[H` and `^[[F` instead of VT220-style `^[[1~` and `^[[4`. This is for compatibility with xterm's default configuration and the xterm termcap/terminfo entries, which means that Home and End should work out-of-the box now in bash, i.e. configuring them in ~/.inputrc is no longer necessary.
  * The Reset menu command and the `^[c` ('Full Reset') control sequence now clear the scrollback as well as the screen.
  * The manual page has gained a tip on using 'sh -c' for setting environment variables in mintty shortcuts.

### 0.3.6 (14 Mar 2009) ###
  * Added a manual page: 'man mintty'. It documents all the options andkeycodes and also has a section with tips on MinTTY usage. Big thanks to Lee D. Rothstein for his help with this.
  * Added command line options for initial window size and position: --size and --pos.
  * Straightened out some inconsistencies in MinTTY-specific keycodes (which are now documented in the man page).
  * Fixed crash when encountering unknown long command line option.
  * Fixed mousewheel overreporting.
  * Fixed incorrect encoding of modifier keys on mouse events.

### 0.3.5 (20 Jan 2009) ###
  * The --config option was unusable, because MinTTY would attempt to invoke '--config' as the child process command.
  * The user's default shell can be invoked as a login shell by passing '-' (a single minus character) as the command.
  * Word selection (by double-clicking) now includes the tilde character in filenames.
  * Added command line option for setting the initial window title.
  * Added short versions of command line options.
  * Improved help output.

### 0.3.4 (14 Jan 2009) ###
  * In 'application cursor mode' the cursor keys sent xterm-incompatible keycodes when combined with a modifier, starting the sequence with ESC O instead of ESC [. This means that combinations such as `<C-Up>` should now work out-of-the box in vim. Nevertheless it's an incompatible change, so apologies to anyone who adapted their scripts to MinTTY's incorrect behaviour.
  * There was a crash when scrolling beyond 32K lines. (The scrollback limit is actually set to 64K lines, not 16K as previously mentioned.)
  * The Linefeed/Newline Mode (LNM) parameter was ignored.
  * When selecting multiple lines, the first character on the last line was always included, which was a bit annoying.
  * Signals were not processed immediately.
  * MinTTY keeps its window open when the command it runs reports failure, so that any error output can be read (which is useful e.g. with ssh). However, this did not deal with signals properly. Now it will stay open only if the command exits with non-zero status or is terminated by a runtime error signal such as SIGSEGV or SIGILL.

### 0.3.3 (8 Jan 2009) ###
  * Fixed window flicker caused by "Disable transparency when active" feature.
  * The "Alt key on its own sends `^[`" setting is no longer ignored.
  * Fixed the shortcut for the "System Default" font smoothing option.

### 0.3.2 (4 Jan 2009) ###
  * Pasting of multiple lines into apps like vi works properly.
  * F1 to F4 send xterm-compatible VT220-style keycodes.
  * The first click on the options dialog is no longer ignored.
  * The scrollbar is shown by default.
  * Closing on Alt-F4 can be disabled (on the Window panel).
  * Characters can be entered via Alt+Numpad codes. Extending on the standard Windows behaviour, codepoints beyond 255 are supported and octal codes can be entered by typing zero as the first digit.

### 0.3.1 (1 Jan 2009) ###
  * Fixed broken non-ASCII output.
  * Increased default font size to 10.
  * Added accelerator keys to options dialog.
  * Added option to switch off transparency when the window has the focus.

### 0.3.0 (29 Dec 2008) ###
  * First officially versioned release. (Previous version numbers were made up retrospectively.)
  * Announced to Cygwin list.

### 0.2.x (Oct-Dec 2008) ###
  * Renamed project, first to "Commando", then "MinTTY".
  * First public release on Google Code on 9 Dec 2008.
  * Redesigned options dialog and menu.
  * Cleaned up and simplified internals, removing lots of unnecessary code.
  * Added shortcut creation script to help with install.

### 0.1.x (Apr-Jun 2008) ###
  * Replaced PuTTYcyg's cthelper backend with direct pty integration.
  * Project is imaginatively named "Terminal".
  * Adopted KDE Konsole's icon.
  * Butchered PuTTY options dialog.
  * Rewrote keyboard handling.

### 0.0.x (Jan-Mar 2008) ###
  * Hacked around on PuTTYcyg.
  * Added xterm modifier encoding.
  * Removed non-Windows ports.
  * Removed networking support.