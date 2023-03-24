### 3.6.4 (25 Mar 2023) ###

Terminal features
  * Tweak response code to XTQALLOWED OSC 60 and XTQDISALLOWED OSC 61 (xterm 378).
  * Readline mouse modes (DECSET 2001 / 2002 / 2003, xterm 379).
  * Hover and open URL: support parentheses (#1196).
  * Fix cursor artefacts in connection with ClicksPlaceCursor=yes.
  * Fix mouse-paste while still selecting.
  * Support multi-line progress detection.

Windows integration
  * Manage user-set HOME for calling Windows from WSL (mintty/wsltty#324, ~mintty/wsltty#76).
  * Support OSC 7 directory cloning if cloning WSL window while in rootfs.

Configuration
  * Status line is configurable in Options menu, switchable from context menu.
  * New user-definable function toggle-tabbar (#1201).
  * Setting ClicksPlaceCursor presets all readline mouse modes.
  * Dynamic support for flags emojis, deployment script (~mintty/wsltty#321).
  * `MINTTY_DEBUG=C mintty ...` will list loaded config files (#1181).
  * New option ProgressScan.

### 3.6.3 (18 Dec 2022) ###

Terminal features
  * Fixed double-width characters not to wrap if auto-wrap disabled.
  * TAB may wrap to next line, or cause subsequent wrap, if enabled (#1182).
  * Fixed auto-wrap behaviour in double-width lines.
  * Overstriking character writing mode also switched with DECSET 20 (VK100, #1184).
  * Fixed OSC 7 (set working directory) to handle ~ prefix.
  * Added XTQMODKEYS state query (xterm 373).
  * Withdrawn DEC private SGRs (/#1171, conflict with XTQMODKEYS).
  * Added XTQALLOWED OSC 60 and OSC 61 (xterm 373).

Unicode and Emoji data
  * Extend emoji information by considering Unicode file emoji-test.txt.

Configuration
  * Option WrapTab (#1182).
  * New user-definable function toggle-opaque (#1168), replaces transparency-opaque.

Other
  * Fixed crash in Options dialog when Printer Service is stopped (#1185).

### 3.6.2 (13 Nov 2022) ###

Unicode and Emoji data
  * Unicode 15.0 update.

Terminal features
  * Status line area support (VT320, xterm 371), DECSSDT, DECSASD.
  * Extended multi-line host-writable status area, DECSSDT 2 N.
  * Combined sub/superscript attributes render small script (#1171).
  * Adjusted subscript position (~#1171).
  * Alternative DEC private SGRs for sub/superscript (#1171).
  * Revamp line cursor handling, size changeable by CSI ? N c (#1157, #1175).
  * Support DECSET 117 (DECECM, VT520).
  * Added DECARR to DECRQSS.
  * Prevent font zooming for resizing controls like CSI 8.
  * Optionally visualize margins by dimming.

Keyboard handling
  * Not suppressing user-defined KeyFunctions for keypad keys in keypad modes (#1161).
  * Alt+keypad-minus initiates decimal numeric input in case an Alt+numpad-digit key is assigned a user-defined function.

Mouse handling
  * Configurable modifiers for hovering and link opening (#1169).
  * Support super and hyper modifiers with mouse functions.
  * Fixed mouse pixel coordinates limits (DECSET 1016).

Initialisation
  * Grab focus again after showing the window, reducing focus delay for Windows 11 (#1113).

Configuration
  * Option OldKeyFunctionsKeypad (~#1161, not listed in manual).
  * Option OpeningMod (#1169).
  * New user-definable function reset-noask.
  * Option DimMargins, user-definable function toggle-dim-margins.
  * Option StatusLine, user-definable function toggle-status-line.
  * Background image mode '+' for combined scaling and tiling (#1180).
  * New user-definable function transparency-opaque (#1168).

Other
  * Fixed crash condition on user-defined commands (#1174).
  * Add confirm dialog to Reset triggered by menu or Alt+F8 (#1173).

### 3.6.1 (24 April 2022) ###

Window handling
  * Terminal reflow (#82, #219): fixed crash condition on irregular reflow chunks.
  * Terminal reflow (#82, #219): fixed memory leak.

Terminal features
  * Visual input feedback: don't obscure text when just pressing Alt.
  * Separate foreground and background values for ANSI colours (#1151).
  * OSC 7704 for setting ANSI colours distinct from palette colours (#1151).

Keyboard handling
  * Support longer multi-char keyboard input (a.k.a. "ligatures" in Windows) (#1155).

Configuration (contributed by Andy Koppe)
  * ANSI colour specification accepts foreground ; background values (#1151).
  * Tuned themes helmholtz (default) and kohlrausch for legibility (#1156).

### 3.6.0 (20 March 2022) ###

Highlights
  * Optional feature: Reflow terminal lines when resizing terminal width.
  * Visual feedback of numeric or composed character input.
  * New themes helmholtz and luminous (contributed by Andy Koppe).
  * Setting helmholtz theme as default colour scheme.

Window handling
  * Reflow terminal lines when resizing terminal width (#82, #219, mintty/wsltty#154).

Terminal features
  * Tweak pending auto-wrap state affected when switching wrap modes.
  * Fixed unscroll (CSI +T) in case width has changed meanwhile.
  * Visual feedback of numeric or composed character input.

Keyboard handling
  * Support user-defined mappings for Super-/Hyper-modified character keys.
  * Support optional user-defined mappings for Control-/Alt-modified character keys (#399, #252, #523, #602, #645, ~#524, ~#817, ~#451).
  * Suspend shortcut handling during numeric character input.
  * Revise and fix numeric character input.
  * Distinct Unicode vs. hexadecimal numeric character input.
  * Menu key opens menu inside terminal, also stops numeric input.
  * Fixed Compose sequences with more than 2 characters (broken since 3.1.5).

Font rendering
  * Right-to-left font fallback (#1148).
  * Fixed auto-widening of ambiguous-width letters (broken since 3.4.4).
  * Speedup rendering of replacement indication of invalid character codes (#1145).
  * Extend cell zoom to some Geometric Shapes U+25E2.., Dingbats U+1F67C.., and Symbols U+1FB00..U+1FBB3.

Startup
  * Trimming irrelevant and possibly confusing environment variables before child invocation (xterm).

Configuration
  * New themes helmholtz and luminous (contributed by Andy Koppe).
  * Setting helmholtz theme as default colour scheme.
  * New option -Rt to report the tty name of the child process / shell.
  * New option ShootFoot (#399, #252, #523, #602, #645, ~#524, ~#817, ~#451).
  * New option RewrapOnResize (#82), interactive (Options dialog).
  * New user-definable function unicode-char.
  * Updated X11-derived data: compose sequences and colour names.

### 3.5.3 (3 February 2022) ###

Terminal features
  * Fixed combining characters colour rendering (~#710).

Desktop integration
  * WSL path conversion considers extended /etc/fstab entries (#1130).
  * WSL path conversion supports UNC paths (#1130).
  * Clipboard: strip terminating NUL (#1132).

Window handling
  * New key shortcut Shift+Shift+Alt+F2 (both Shift keys) to enforce new window outside tabbar.
  * Distinct system menu items "New Window" and "New Tab" if tabbar enabled (mintty/wsltty#295).
  * Limiting size of scrollback buffer to secure buffer and clipboard handling (#1134).
  * Avoid position gap after Options Apply (~#1126) in more cases, especially scrollbar toggling.
  * Always fix window position back to screen when widening beyond screen.
  * Save as Image (from Ctrl+right-click) with Shift also opens the image (#1139).
  * Horizontal scrolling feature (#138).

Font rendering
  * Tweak again handling of negative font leading (#948, #946).

Hotkey functions and user-definable functions
  * Deprecated default key assignments Control+Shift+N / +T / +P.
  * New user-definable functions new-tab and new-tab-cwd.

Configuration
  * New option NewTabs (mintty/wsltty#295).
  * New command-line option --newtabs.
  * New option MaxScrollbackLines.
  * Option OldModifyKeys to tune modified special keys.
  * New command-line option --horbar.
  * New user-definable functions for horizontal scrolling.

### 3.5.2 (13 November 2021) ###

Unicode and Emoji data
  * Unicode 14.0 update.

Terminal features
  * Fix (revert back) DECSDM (DECSET 80) Sixel Display mode (#1127, xterm 369).
  * Sound file playing OSC 440 (#1122).
  * DECPS tone playing support (#1122).
  * Fixed LED state glitch when ScrollLock is held in auto-repeat.
  * Extended scope of area attributes change functions DECCARA and DECRARA.
  * Unscroll sequence CSI +T, filling lines from scrollback buffer (kitty).
  * Changed default BracketedPasteByLine=0 for consistent appearance.

Window handling
  * Fixed -s max... options (#1124).
  * Tweaked handling of positioning and size options.
  * Always support negative position offset (#1123).
  * Avoid position gap after Options Apply (#1126).
  * Copy text: set proper clipboard timestamp.

### 3.5.1 (4 September 2021) ###

Terminal features
  * Visual double-width of symbols and emojis with subsequent space (#1104, #892, #1065, #979).
  * Limit line cursor width by width of lines (underline etc) (#1101).
  * Alternative escape sequence DECSET 2026 for synchronous screen update (#1098).
  * Optimise screen display speed on bell sound series (#1102, ~#865).
  * Italic emojis.
  * Notify child process via ioctl also when scaling window with font size (xterm 368).
  * Bracketed paste mode: configurable splitting by line.
  * New user-definable functions no-scroll, toggle-no-scroll, scroll-mode, toggle-scroll-mode.
  * Management of the ScrollLock LED for consistence with actual status of special scroll features.

Rendering
  * Speedup of width detection for auto-narrowing for certain characters (#1065, #979, #892).
  * Prevent artefacts of large-size underscore cursor (CSI 4 SP q CSI ? 6 c).
  * Prevent spacing anomaly after U+FD3E and U+FD3F.
  * Fix emojis selection highlighting (#1116), reverting 3.0.1 tweak for emojis in bidi lines.

Window handling
  * Ensure -w full to cover taskbar also with -B void (~#1114).
  * Tab management: Keep tabbar consistent (~#944, #699).

Initialisation
  * Font initialisation speedup (~#1113).
  * Avoid duplicate font initialisation (~#1113).
  * Earlier window display by later setup of drag-and-drop and tabbar (~#1113).
  * Grab focus before showing the window, reducing focus delay (#1113).

Configuration
  * New option BracketedPasteByLine.
  * Transparency button slider (#728, #140).
  * New user-definable function new-window-cwd to clone window in current directory (~#1106).
  * New user-definable functions no-scroll, toggle-no-scroll, scroll-mode, toggle-scroll-mode.

### 3.5.0 (16 April 2021) ###

Terminal features
  * Revised and fixed handling of blink attribute (~#1097).
  * Coloured blink substitution display (xterm), escape sequences OSC 5/6;2.
  * Support distinct blink attribute for base and combining characters.
  * Apply blink attribute to graphics.
  * Escape sequence OSC 22 to set mouse pointer shape (xterm 367).
  * Escape sequences DCS=1/2s for atomic/synchronous screen update (~#1098).
  * Support progress detection (for implicit progress bar) also if iconized.
  * Implicit (detected) progress bar uses configured colour.
  * Escape sequences to reset progress bar colour mode to configured value.
  * Escape sequence to change progress value only.

Desktop integration
  * New user-definable function win-toggle-keep-screen-on to disable screen saver.

Configuration
  * New option BlinkColour.
  * New options MousePointer, AppMousePointer.
  * Restored "Allow blinking" in Options dialog (#1097).
  * WSL-specific detection of available settings for Terminal Type (option Term) (mintty/wsltty#278).
  * Export TERM to WSL (mintty/wsltty#278).

### 3.4.7 (16 March 2021) ###

Terminal features
  * Fixed blinking for drawn/overstrike characters, (under)lining and emojis.
  * Bracketed Paste Mode: ensure embedding of each line.
  * Fixed character set GR mappings to be unguarded by NRCS (vttest 3.10).
  * Restore attributes after DECALN test pattern (vttest 11.6.4/5).
  * Simplified support of ISO Guarded Area as protected (xterm-like global distinction).
  * Fixed validity for REP repeat preceding graphic char (vttest 11[.6].7.2).
  * Keyboard status report (DEC DSR 26), reporting window focus (vttest 11.1.1.1.1).

Vector graphics (Tektronix 4014 mode)
  * Support "written first vector", triggered by GS-BEL (vttest 11.8.6.5, ~#896).
  * Initial written vector joins previous text output (xterm).
  * Adjustment of border coordinates to compensate for coordinate rounding.
  * Fixed GIN mode and ENQ coordinates.
  * Tweaked ENQ status byte.
  * Distinct GIN mode mouse input (xterm).
  * Smooth GIN mode crosshair cursor movement.
  * Various mode handling fixes after GIN mode.
  * GIN mode terminator strap options (Tek, xterm).
  * Enhanced "defocused" indication by boldened colour.
  * Enhanced "defocused" point plot indication by boldened point size.

Window handling
  * Lines cleared from top of screen are scrolled into scrollback buffer (mintty/wsltty#272).
  * New user-definable function win-toggle-always-on-top (#1095).
  * New heuristics to adjust row spacing to font metrics (mintty/wsltty#257).

Configuration
  * Run shell in login mode if terminal started from Windows shortcut.
  * New option LoginFromShortcut.
  * New option AutoLeading (mintty/wsltty#257).
  * New option EraseToScrollback.
  * New option TekStrap.

### 3.4.6 (20 February 2021) ###

Configuration
  * Support style Emojis=zoom.
  * OSC 7750 for dynamic change of emojis style.

### 3.4.5 (17 February 2021) ###

Terminal features
  * Fixed width handling when selecting a non-text font (~#777).
  * Auto-narrowing: glyph double-width detection for double-letter characters (like Dz, #777).
  * Support fractional percentage for progress detection (#1074).
  * Tweaked availability of DEC Cyrillic character set (VT520, xterm 363).

Keyboard handling
  * Changed Ctrl+Backarrow to send Backspace (#1082, #449, xterm).
  * Applying modifyOtherKeys mode 2 more consistently to special keys (~~#1082).

Configuration
  * Tool `mintheme` works from WSL and in `screen` (mintty/wsltty#268).
  * Support home or environment variable prefix for setting SaveFilename (~#1086).
  * New settings -P/--pcon/ConPTY to enable/disable ConPTY support (mintty/wsltty#271).
  * Support for theme file conversion on filename drag-and-drop (#1079).
  * Support for theme file conversion on "file:" URL drag-and-drop (~#1079).
  * Support for ".json" theme file conversion (~~#1079).

### 3.4.4 (19 December 2020) ###

Unicode and Emoji data
  * Update to Emoji data version 13.1.

Terminal features
  * Terminal reset clears progress bar (mintty/wsltty#202).
  * DECTST colour fillings (CSI 4;10..13 y, VT240).
  * Smart detection of progress indication also inmidst line (mintty/wsltty#202).
  * Fixed rendering of 0x7F (DEL code) in some 96-characters NRCS modes.
  * Support for 48-bit hex colour specs (#1072).

Font rendering
  * Enabled width detection for auto-narrowing non-BMP characters (#1065).
  * Tweaked character ranges to consider for auto-narrowing (#1065).
  * Enabled secondary font specification for Unicode blocks (#777).

Keyboard handling
  * Compose key may also be user-defined super or hyper (#777).

Window handling
  * Optionally transform Exit to characters, to exit on application-level (#1063).
  * Options dialog: configurable font and size (~#1059).
  * Tweak initial setup of terminal pixel size (#1071).

Configuration
  * New option ExitCommands (#1063).
  * New options OptionsFont and OptionsFontHeight (~#1059).
  * Extended syntax for option FontChoice (#777).
  * New ComposeKey values super, hyper (#777).

### 3.4.3 (11 November 2020) ###

Character encoding
  * Fixed locale setup interworking with bash startup profile (~#1050).
  * Handling empty startup locale to be consistent with system-derived shell locale (~#1050).

### 3.4.2 (4 November 2020) ###

Terminal features
  * Progress bar control sequences CSI %q or OSC 9;4 (experimental).
  * Optional automatic progress detection (mintty/wsltty#202).
  * Media Copy sequence CSI 12 i to dump screen as image (#1051).
  * HTML screen dumps do not visualize Space and TAB.
  * New DECSCUSR (CSI SP q) values 7, 8 to set cursor style box (mintty/wsltty#204).
  * Fixed horizontal position of emojis in double-width lines.

Window handling / Tabbar (thanks to K. Takata)
  * Bring other tab to top when closing (#1054).

Character encoding
  * Fixed locale setup in case of empty locale (#1050, thanks to K. Takata).
  * Always set LANG if option Locale is used (#1050).

Configuration
  * New option ProgressBar (mintty/wsltty#202).
  * New option value CursorType=box (config file/command line only, mintty/wsltty#204).

### 3.4.1 (24 October 2020) ###

Terminal features
  * Fixed injected false visual tab indication (#1036).
  * Escape sequences for stack of colours (XTPUSH/POP/REPORTCOLORS, xterm 357).
  * Support multiple controls in OSC 4, 5, 10..19, 104, 105 (#1038, xterm).

Window handling / Tabbar (thanks to K. Takata)
  * Align position of new tabbar-enabled window with previous one (#1044).
  * Tweaked tabbar handling for speed-up of title changes (#1043).
  * Fixed startup position of maximised window (#1045).
  * Fixed Alt+Shift+F2 and Alt+F2 behaviour on a maximised window (#1045).

Font installation / Portable application
  * Support dynamic installation of temporary fonts (#901, #1004).

Window handling
  * Clipboard selection can optionally contain TAB characters (#1037).
  * Click-open link still after moving the mouse over the link area (#1039).
  * Keep hotkey-started window in taskbar (#1035).
  * Override font zooming also with Ctrl, to support FancyZones (#487).
  * Fixed CopyAsHTML config glitch (#1042).

Character encoding
  * Revised locale handling and setting, especially with option Locale.
  * Revised character width determination and setting, especially with option Locale.
  * Revised setup of GB18030 encoding support.
  * New option to enforce narrow ambiguous-width.
  * Do not clear/overwrite all locale categories anymore by option Locale.
  * Do not enforce UTF-8 encoding with WSL anymore.
  * Propagate locale settings with option --WSL (mintty/wsltty#259).

Configuration
  * New option CopyTab (#1037).
  * New option value Charwidth=ambig-narrow.
  * New option OldLocale.

### 3.4.0 (19 September 2020) ###

Window handling / Tabs
  * Optional tabbar for interactive virtual tabs session switching (#944).
  * Fixed maximised/fullscreen synchronisation among sessions/tabs.
  * Fixed state inconsistencies after minimizing synchronized windows (#699, ~#944).

Window handling
  * Fixed offset of saved image.
  * Fixed themes list and interactive theme switching feedback (mintty/wsltty#251).
  * Support system hotkey for activation in "Quake mode" (#1029).

Mouse handling
  * If ZoomMouse=false, Ctrl+mouse-wheel scrolls by 1 line per notch (#1032).
  * Configurable number of scroll lines per mouse wheel notch (#1032).
  * Support mousewheel reporting (e.g. for shell history scrolling) in normal screen mode (~#1032).
  * Nudge complete delivery of application scrollbar sequences by delay (#1033).

Terminal features
  * Mouse mode 1016 with pixel coordinates (xterm 360).
  * XTPUSHSGR foreground/background changed to 30/31 (xterm 357).
  * Align OSC response terminator (ST or BEL) with request terminator (#1028, xterm).
  * Fixed invalid IME cursor colour after OSC 112 "Reset cursor colour" (#942).

Configuration
  * New option TabBar and command-line option --tabbar (#944).
  * New user-definable functions clear-title, refresh.
  * New option LinesPerMouseWheelNotch (#1032).
  * Option KeyFunctions: flexible specification of modifiers (in any order) (#851).

### 3.3.0 (6 August 2020) ###

Vector graphics
  * Tektronix 4014 terminal vector graphics terminal emulation (#896).

Keyboard handling
  * Cancel compose key on mouse actions, to prevent surprising character composition.
  * Fixed special key assignment (Ctrl+Shift+^) spoiled by KeyFunctions (since 2.9.1).
  * Fixed special key assignment (Ctrl+Shift+@ with AltGr) spoiled by AltGrIsAlsoAlt prevention (since 3.1.5).
  * Fixed Ctrl+AltGr+letter combinations.
  * Fixed key modifier matching for KeyFunctions in some cases of Super/Hyper.
  * Dropped Ctrl+Enter special key assignments.
  * Deprecated Shift+Escape/Break/Pause.

Window handling
  * Prevent mouse wheel double interpretation, also fixing speed issues (mintty/wsltty#238).
  * Handle proper link attributes when screen is scrolled (#1021).
  * Image saving feature for terminal contents (png format), DEC and Tek.
  * Fixed Shift+Alt+F2 size control on maximised/fullscreen windows (#633).
  * Fixed Alt+F2 on normal window to clone current size (broken since 2.4.3).
  * Tweak click-opening WSL files (mintty/wsltty#115).

Configuration
  * Support resource configuration directories via links (#1016).
  * New option PrintableControls to make controls visible (#1019).
  * New options Tek*.
  * New user-definable functions save-image, tek-copy, tek-page, tek-reset.
  * New option SaveFilename.

### 3.2.0 (20 June 2020) ###

Sixel and image display
  * For overlapping images, fixed background and clipping borders (~#1010).
  * Avoid image flickering by revised image list rendering strategy (#1010).
  * New strategy to detect and collect overlaid images (~#1010).

### 3.1.8 (7 June 2020) ###

Terminal features
  * Handle new lines within OSC strings, ignore for image data (#1010).

Keyboard handling
  * Optional legacy Alt modifier fallback for AltGr key.

Window handling
  * Fixed crash with empty search pattern (#1011).

Configuration
  * Changing default CheckVersionUpdate=0 to disable version checking by default.
  * Option AltGrIsAlsoAlt.

### 3.1.7 (6 June 2020) ###

Terminal features
  * New default CSI - p (DECARR 0) to disable keyboard repeat rate limitation.

Keyboard handling
  * Keyboard auto-repeat handling is now as usual (not affected) by default (#1009).
  * Tweaked auto-repeat rate adjustment to better reflect selected values.
  * Configurable key to activate keyboard selecting mode (#997, #84).
  * Restored right-Alt modifier (if not AltGr) (#1003, ~#969).

Window handling
  * Ctrl+Alt+mouse-click restored to move the window (#1008).
  * Search results are cleared on reset (#998).
  * Constrain dark mode handling (#983) to Windows 10 Version 1809 (#1001).
  * Alt+F2 clones maximised and fullscreen window modes (#999, ~#633).

Configuration
  * New user-definable function kb-select (#997, #84).

Other
  * Adding Windows build information to About info.

### 3.1.6 (21 May 2020) ###

Window handling
  * Fixed resource leak when displaying images (#995).
  * Fixed crash condition on keyboard auto-repeat (#996).

### 3.1.5 (21 May 2020) ###

Terminal features
  * Limit coordinates of all mouse reports to text area (#972).
  * Support bell volume escape sequence DECSWBV (#974) with wave files.
  * Support margin bell feature (#974), with DECSET 44 enabling sequence (xterm).
  * Support margin bell volume escape sequence DECSMBV (#974) with wave files.
  * Support visible space indication.
  * Fixed insert mode in single-width character mode (#964).
  * DECFRA (fill rectangular area) works with wide and non-BMP characters.
  * DECFRA works with NRCS and DEC Line Drawing characters (VT420).
  * New CSI > 0 q to report terminal name and version (#881).
  * New CSI 2/1 SP Z (ECMA-48 PEC) as character attribute to enforce single/double width (#615, #979, #973, #938, #638, ~#88, ~#671).
  * New CSI cps - p DECARR Select Auto Repeat Rate (VT520).
  * Support Ctrl+Alt-mouse clicks (#987).

Character encoding
  * Enhanced and documented GB18030 support (mintty/wsltty#224).

Keyboard handling
  * Unmapped AltGr combinations: don't fallback to ESC prefixing (~#969).
  * Guard Compose key against hotkey injection (~#877).

Unicode and Emoji data
  * Unicode 13.0 update.
  * Distinct support for EmojiOne (from unicode.org) and JoyPixels emojis.

Font rendering
  * Enable auto-narrowing for Private Use characters (#979, "Nerd Fonts").
  * Adjust and zoom rendering for geometric Powerline symbols (#979).
  * Support emoji style OpenMoji (#985).

Window handling
  * Tweak title bar dark mode adaptation to undocumented Windows changes (#983, mintty/wsltty#157).
  * Support dark mode for menus (#983, mintty/wsltty#157).
  * DropCommands secured against multiple placeholders.
  * DropCommands optionally pastes Windows format paths (#982).
  * Drag-and-drop import of itermcolors schemes (mintty/wsltty#227).
  * Workaround to interact with Hot Keyboard (#877, setting SupportExternalHotkeys=4).
  * Disable deprecated glass mode from interactive options (#501, mintty/wsltty#241).
  * Speedup of scrollback searching (#988, #986).

Other
  * Warning and error popups are placed on top of desktop.
  * Environment variables TERM_PROGRAM[_VERSION] provide unreliable info about terminal application (#776, #881).

Configuration
  * Options BellFile2 ... BellFile7 (#974).
  * Options DispSpace, DispTab, DispClear.
  * Options Suppress* are now resilient against space (mintty/wsltty#235).
  * Option value SupportExternalHotkeys=4 (#877).
  * Option value Emojis=openmoji (#985).

### 3.1.4 (25 February 2020) ###

Terminal features
  * Linux console controls for underscore cursor size (mintty/wsltty#203).
  * Fixed sixel display failures at certain sizes (#967).

Window handling
  * Avoid resize flickering loop in Virtual Tabs mode.
  * Don't keep selection size info popup on top when defocussed (#956).
  * Ctrl+mouse on title bar can be configured or disabled (#953).

Font rendering
  * Fix adjusted row spacing ("Leading") for negative font leading (#948, #946).
  * Support enabling of additional ligatures such as arrows (#601).
  * Search bar button symbols configurable (#955).
  * Support optional enforced single-width character rendering (#964).
  * Not widening ambiguous-width Geometric Shapes, to avoid unsymmetric pairs.
  * Proper auto-widening (æ, œ, ...) in partially ambiguous-wide fonts (MS Mincho).

Configuration
  * New option Ligatures (#601).
  * New options MenuTitleCtrlLeft, MenuTitleCtrlRight (#953).
  * Option SearchBar can configure button symbols (#955).
  * New option values Charwidth=single / Charwidth=single-unicode (#964).

Other
  * Fix access to shortcut icon with path prefix %ProgramW6432%.
  * Fix clipboard memory handling (#950).
  * Note that DropCommands does not work with WSL or remotely (#947).

### 3.1.0 (23 November 2019) ###

Terminal features
  * Graphic image output support (iTerm2).
  * Fixed Sixel image management (#929).
  * Fixed premature discarding of partially scrolled-out image.
  * Glyph checking (OSC 7771) considers FontChoice.

Keyboard handling
  * Fixed AltGr handling for AutoHotKey (#932).

Window handling
  * Flush notification to handle auto-repeat click on scrollbar.

Font rendering
  * Adjust row spacing ("Leading") for high DPI monitors (#777).
  * Meta-script names "CJK" and "Private" for FontChoice (#928, #943, ~#754).
  * Adjusted default value CharNarrowing from 80 to 75 (#922).

Other
  * Collect font warnings into one popup message.

Configuration
  * Bold handling configuration makes "xterm" fallback explicit (#939, #936, #468).

### 3.0.6 (6 October 2019) ###

Terminal features
  * Support for horizontal mouse scrolling (#925).
  * Fixed Sixel display which was broken in 3.0.3.

Font rendering
  * Changed default value CharNarrowing from 70 to 80 (#922).
  * Excluding arrows, technical symbols, and other ranges from auto-narrowing (#922).

Virtual Tabs
  * User-definable key bindings for quick tab switching (#923, #699).

Other
  * Safe handling of missing shortcut title/appid (#920).

Configuration
  * New user-definable functions switch[-visible]-(prev|next) (#923).

### 3.0.5 (2 October 2019) ###

Configuration
  * Fix handling of --WSL default distribution.

### 3.0.4 (1 October 2019) ###

Font rendering
  * Exclude full-cell characters from auto-narrowing (#918).
  * Fix adjustment of auto-narrowed characters (#918).

Configuration
  * Emojis - Placement values translatable (#919).

### 3.0.3 (28 September 2019) ###

Terminal features
  * Maintain scrollback buffer in left/right margin mode with default margins (#916).
  * Do not put cleared lines into scrollback buffer.
  * Fixed display of subsequent identical emojis (since 3.0.1).
  * Mouse buttons 4 and 5 send the same escape sequences as xterm.
  * Disabled unreliable CSI DECLL to switch keyboard LEDs (#915).
  * DECAUPSS to assign user-preferred supplemental sets to DECSUPP.
  * Ignore SOS string (ESC X ...), in addition to PM and APC (xterm).
  * Fixed iconified window report which was reverted (#893).
  * Prevent negative CSI 13 t response parameters (#899).
  * Fixed interaction of OSC 12/112 "Set/Reset cursor colour" with IME (#903).
  * Fixed text area size reported in response to CSI 14 t (#899).

Font rendering
  * Reimplement auto-narrowing (too wide glyphs) by coordinate scaling (#892).
  * Support alternative font choice for symbols and pictographs (#892).
  * Option to adjust automatic character narrowing (#892).
  * Bloom effect around characters of old CRT terminals, rough simulation.

Window handling
  * Virtual Tabs support on title bar (#699).
  * Flexible configuration of scroll modifiers (~#894).
  * User-definable functions for scrollback scrolling (~#894).
  * Fixed Sixel image list handling (#905).
  * Optimized Sixel image rendering if overlapped (#905).
  * Dark colour theme support in scrollbar (mintty/wsltty#157).
  * Clear resizing popup after leaving fullscreen and moving (#913).

Other
  * Cache emoji image data (speedup emoji display).
  * Dropped PATH dependency of printer feature (#897).
  * Dropped float: left from copied HTML style (#900).
  * Fixed Windows handle resource leak when displaying many emojis (mintty/wsltty#185).
  * Fixed potential crash on Sixel display after resource leak.
  * Preventing Windows handle resource exhaustion when displaying many Sixel images.
  * Fixed potential crash when confirming exit (#907).

Configuration
  * Option Bloom.
  * Option KeyFunctions can assign scrollback scrolling keys (~#894).
  * Option CharNarrowing (#892).
  * Options dialog supports setting Emojis and EmojiPlacement.
  * Options dialog: additional "Selection" panel with additional settings.
  * Option OldOptions to customize changed areas in Options dialog.
  * Options -Rp and -RP to report process IDs (#909).

### 3.0.2 (13 July 2019) ###

Terminal features
  * Application scrollbar (experimental).
  * Control sequence to switch IME status (#888, Tera Term).
  * ECMA-48 SL/SR shift columns left/right (xterm).
  * Fixed overstrike in leftmost column.
  * Inhibiting double width/height lines in left/right margin mode.
  * Primary DA indicate Horizontal Scrolling and Rectangular Editing.
  * Secondary Device Attributes report Unicode version with option Charwidth.
  * Report DECRQSS DECSCL conformance level as VT500 (7-bit controls).

Terminal controls verified and tweaked as suggested by esctest suite
  * Rectangular checksum DECRQCRA, supporting esctest suite.
  * Fixed BS and CR "border" cases.
  * Fixed DCH/ICH/IRM outside left/right margins.
  * Fixed DL/IL to move cursor to left margin.
  * Reverse wrap from home position moves to lower right margin (xterm).
  * Auto-wrap modes are no more affected by cursor save/restore.
  * NRC enabled mode is no more affected by cursor save/restore.
  * Fixed left/right margin mode to inhibit double width lines.
  * Fixed TAB to stop at right margin.
  * Ensure restored cursor to be within margins in origin mode.
  * Set/Reset origin mode moves cursor home.
  * Confining DL, IL, DECDC, DECIC within margins.
  * Confining LF, IND, VT, NEL, FF within margins.
  * Fixed DECSED 3 not to clear current position; guard CSI [?] n J/K.
  * Proper default values for rectangular operations.
  * Full reset (RIS) moves cursor home.
  * Soft reset (DECSTR) does not disable left/right margin mode.
  * Cursor backward (CUB) applies reverse-wraparound.

Font rendering
  * Script-specific secondary font choice (#580, #821, #883).
  * Reenabled DPI scaling in Windows 7 (#890).
  * Check functions (width, glyph) consult proper attributes and font.
  * Tweaked check for automatic narrowing to fit in cell width.
  * Fixed shadow attribute artefacts.
  * Fixed DEC Tech up/down arrows by manual drawing.

Sixel graphics
  * Tweaked Sixel handling to avoid crash condition.
  * Reintroduced fixed Sixel colour registers handling (#593).

Window handling
  * Reenable left scrollbar.
  * Application scrollbar (experimental).
  * Always flash taskbar on bell if iconic, if configured (#887, #607).

Other
  * Reduced global HTML formatting on Copy as HTML text (#889).

Configuration
  * Option Baud to simulate serial connection speed for a legacy feeling.
  * Option FontChoice for script-specific secondary fonts (#580, #821, #883).

### 3.0.1 (28 May 2019) ###

Highlights
  * New character attributes superscript, subscript, shadowed, overstrike.
  * DEC VT420 screen control features.
  * Fully VT100-compatible, including VT52 mode (with graphics).
  * Up to 6 key modifiers, including Meta (Win key) and configurable Super and Hyper keys.
  * Bell sounds in package.
  * Various window and clipboard handling optimisations and extensions.
  * User-definable function extensions.

Terminal features
  * New character attributes overstrike and (as inspired by existing terminfo capabilities) shadow, subscript, superscript:
    * SGR 8:7 overstrikes characters over preserved screen contents (28 clears).
    * SGR 1:2 for shadowed display (22 clears).
    * SGR 73 and SGR 74 for superscript and subscript display (75 clears).
  * VT420 rectangular area copy and fill: DECCRA, DECFRA, DECERA, DECSERA.
  * VT420 area attributes: DECSACE, DECCARA, DECRARA.
  * VT420 left and right margins: DECSLRM and private mode 69.
  * VT420 horizontal scrolling: DECBI, DECFI, DECIC, DECDC.
  * VT510 DECNCSM.
  * Primary Device Attributes report VT420 by default; VT525 supported.
  * Blinking cursor mode (DECSET 12) overlays blinking cursor style (DECSCUSR) so that DECRST 12 does not spoil blinking style (#818, mintty/wsltty#133).
  * Support ECMA-48 SPD control sequence, values 0 and 3 (RTL fun feature).
  * Support DECSET 1007 (mouse wheel reporting, xterm).
  * New CSI # p/q XTPUSHSGR and XTPOPSGR aliases (xterm 345).
  * Fixed DECSET 80 (Sixel scrolling mode) which was reverted.
  * Clear All Tabs sequence (TBC 3) extends after resizing, compatible with xterm.
  * VT52 mode, thereby becoming fully VT100-compatible, with Atari ST extensions.
  * HP Memory Lock/Unlock (xterm).
  * Added DECSNLS to DECRQSS.
  * Fixed VT and FF in LNM mode.
  * Fixed format of OSC 5;0;? response.

Keyboard handling
  * Fixed encoding of Win key as Meta modifier.
  * Support optional Super and Hyper modifiers.
  * Lock keys do not change the state if they have user-defined assignments.
  * User-definable additional function keys.

Window handling
  * Revised/fixed fullscreen/font zooming behaviour and effect of ZoomFontWithWindow.
  * Fixed Default Size function in some cases after previous fullscreen zooming.
  * Search (Alt+F3) can keep search context distance from top/bottom (#797).
  * Optional squashing of bell sound series (#865).
  * No limited font warning if the selected locale is likewise limited (~#871).
  * Suppress output rendering while window is iconic (~#875).

Clipboard handling
  * Trim trailing spaces for HTML and HTML text copies (#878).
  * Select HTML detail level by setting of "Copy as HTML" alone (#878).
  * Specify "fixed pitch" for RTF format (~#878).
  * New options CopyAsRTFFont, CopyAsRTFFontHeight (~#878).

Configuration
  * Mintty packages include a selection of bell sounds (#711).
  * New user-definable key name CapsLock.
  * New user-definable key name prefix ‘*’ to define for all modifiers.
  * New user-definable modifier key assignments super and hyper.
  * New user-definable function paste-path (mintty/wsltty#161).
  * New user-definable control char syntax (#873).
  * New prefixes U/Y (for super/hyper) for user-defined keys.
  * New user-definable numeric action, entering a function key sequence.
  * New option BellInterval to squash BEL sequences (#865).
  * New option SearchContext (#797).
  * Offer terminal types vt420, vt525; also xterm-direct, mintty, mintty-direct if installed (~#866, ~#867).
  * With --WSL, initialise title to WSL name rather than command line (~mintty/wsltty#167).

### 3.0.0 (28 March 2019) ###

Character processing
  * Fixed wide character width and cursor position handling.

Keyboard handling
  * Switchable auto-repeat; DECSET 8 (DECARM), option AutoRepeat, toggle function.

Bidirectional rendering (Unicode Bidi Algorithm)
  * Bidi bracket pairs: implemented UBA rule N0.
  * Minor fixes to UBA rules X9, W7, L1.
  * Fixed shortcut optimization wrongly not triggering UBA on Arabic numbers.

Terminal features
  * Cursor style control (DECSCUSR) can set the blinking interval with an optional second parameter.

Configuration
  * Option AutoRepeat.
  * New user-definable function toggle-auto-repeat.

### 2.9.9 (16 March 2019) ###

Keyboard handling
  * Fixed modifyOtherKeys mode 1 to use verbatim control keys again (#860).

### 2.9.8 (15 March 2019) ###

Unicode and Emoji data
  * Unicode 12.0 update.

Keyboard handling
  * Fixed control-key reporting in modifyOtherKeys mode to use small letters.

### 2.9.7 (15 March 2019) ###

Highlights (details see below)
  * Significant improvements in bidirectional handling.
  * Text can be selected with the keyboard.
  * Explicit hyperlink attributes.
  * Avoid keyboard/echo latency.

Bidirectional rendering
  * Fixed handling of double-width chars within RTL.
  * Fixed handling of neutral chars in first or last position (UBA rule N1).
  * Updated RTL mirroring data, generating them from Unicode.

Terminal features for ECMA-48 and other bidi control
  * Support BDSM control sequence (SM/RM bidirectional support, CSI 8 h/l).
  * Private mode DECSET 2501 for "autodetection of direction" (UBA rules P2/P3).
  * Support SCP control sequences (LTR/RTL "Character Path").
  * Private mode DECSET 2500 for "box mirroring".
  * Bidi direction detection on paragraph level (wrapped lines).

Other terminal features
  * Support for OSC 8 hyperlink attribute (~#823).
  * Providing DECTABSR tab stop report.
  * Fixed DECRQM 12 which was inverted.
  * DEC Cyrillic NRCS (xterm 344).

Character rendering
  * Fixed and tweaked wavy underline / undercurl (#847).

Keyboard handling
  * Keyboard selecting mode (#84).
  * Avoid keyboard/echo/display update latency.
  * modifyOtherKeys mode supports control chars in non-Latin keyboard layout.
  * Fix modifyOtherKeys mode 1 to support Ctrl+AltGr.

Mouse link hover and click handling
  * Consistent hover highlighting.
  * Overriding modifier is also accepted in non-application mouse mode (#694).

Window handling
  * Fixed option --Border=void/frame (#843).
  * Fixed start from shortcut in Windows XP.

Documentation
  * Wiki Tips: note about UTF-8 requirement for emoji support (#842).
  * Fixed description of option HandleDPI (#824, #774, #853).

Configuration
  * Support for Win key modifier for user-defined keys (option KeyFunctions).
  * Support for Win key modifier for options ScrollMod and ClickTargetMod.
  * New option HoverTitle.
  * New user-definable function toggle-bidi.
  * Dropping CR from missing option error message.
  * Added FF and ESC to FilterPasteControls characters (xterm 344).

### 2.9.6 (20 January 2019) ###

Terminal features
  * Fixed bidi "run" handling (~#837).
  * Fixed bidi embedding handling (#837).
  * HTML export/copy: Fixed HTML style attributes.

Window handling
  * Flexible window grouping configuration (#789).
  * If started from desktop shortcut, clone AppID from it (#784, mintty/wsltty#96).
  * Display speedup by skipping refresh intervals (#835).
  * Support for pasting from Windows clipboard history (mintty/wsltty#139).
  * Option to lock title from being changed (mintty/wsltty#138).

Keyboard handling
  * Workaround for buggy StrokeIt tool sending right-Alt+Fn key events (#833).
  * Optional support for external hotkeys (esp. to close window), overriding disabled Alt+Fn shortcuts.
  * Workaround for Windows clipboard history pasting implementation (mintty/wsltty#139).
  * Unified environment for external commands attached to keys (KeyFunctions) with those in context menu (UserCommands).

Configuration
  * New option UserCommandsPath to configure PATH for UserCommands, KeyFunctions, SysMenuFunctions.
  * Option Class supports the same placeholders as AppID (#789).
  * New option SupportExternalHotkeys.
  * New option DisplaySpeedup (#835).
  * New options CtxMenuFunctions and SysMenuFunctions to customize menus (#820).
  * New user-definable functions lock-title, new-window, win-toggle-max.

### 2.9.5 (5 December 2018) ###

Window handling
  * Fixed startup directory after cloning new window after starting from desktop shortcut (#784, mintty/wsltty#96).
  * Avoiding stale hover indication in unfocussed window.
  * Changed default handling of resolution change to HandleDPI=2 (#824).

Tweaks to HTML clipboard/export feature
  * Flexible HTML formatting levels.
  * Configurable, also in Options dialog.
  * No more table cell container.
  * HTML escaping.
  * Apply styles individually and other tweaks for increased compatibility.
  * Font fallback 'monospace'.
  * Find relative HTML file name on Shift+"HTML Screen Dump".

Configuration
  * CopyAsHTML (#825, #811).

Other
  * Ensuring /bin in PATH for user commands.

### 2.9.4 (10 November 2018) ###

Terminal features
  * Copy as HTML (#811).
  * Mitigate stalling on very long paste buffer lines (#810).
  * New CSI DECLL (VT100, xterm) to switch keyboard LEDs (and their associated modifier function).
  * New CSI > 0/2 p to switch option HideMouse (xterm pointerMode).

Appearance
  * Option Background== for floating window effect (using desktop wallpaper as background) (#18, ~#666, ~~#501).

Window handling
  * Fixed suspend-output-while-selecting buffer, size is configurable (#816, ~#799).
  * Consider glyph width for font width determination (#808).
  * Do not start process to construct process list for exit confirmation (~#448).
  * Enhanced taskbar icon grouping behaviour (#784, mintty/wsltty#96, ?#495, ?#420, ??#801).
  * Setting MINTTY_SHORTCUT when started from a desktop shortcut.
  * Maintain proper terminal size after DPI change in DPI awareness mode V2 (#774).

Configuration
  * AppID supports placeholders for flexible customization of taskbar icon grouping behaviour (#784, mintty/wsltty#96, ?#495, ?#420, ??#801).
  * Option SuspendWhileSelecting to set the max size of the suspend-output-while-selecting buffer (#816, ~#799).

### 2.9.3 (4 October 2018) ###

Terminal features
  * Fixed failing recognition of single-char ESC sequences.

Terminal interaction
  * Enhanced ligature support redisplays previous cursor line (#601, mintty/wsltty#123).
  * Support switching rectangular mode while mouse-dragging selection.

Configuration
  * Option Enable132ColumnSwitching to enable 132/80-column switching initially (#196).
  * Option value LigaturesSupport=2 (#601, mintty/wsltty#123).

### 2.9.2 (3 October 2018) ###

Terminal interaction
  * Fixed space consideration for wrap/resize/copy handling (#800, ~#82).
  * Optionally include trailing space in selection (~#768, ~#800).
  * Reduced occasional flickering by buffering terminal input (#799).
  * Sanitized output buffering during selection (~#799).
  * Ligatures display support while being input (mintty/wsltty#123, #601).
  * Optionally suppress mouse wheel effects (#170).

Terminal features
  * Additional 96-character NRCS (xterm 336).
  * Fixed "Latin-1/UK" NRCS.

Window handling
  * OSC I to set icon from file (shelltool, dtterm, xterm 333).
  * OSC l to set window title (shelltool, dtterm, xterm 333).

Configuration
  * Option LigaturesSupport (mintty/wsltty#123, #601).
  * Option SuppressMouseWheel (#170).
  * Option TrimSelection (~#768, ~#800).
  * Support multi-line splitting for all key:value list options.

### 2.9.1 (20 September 2018) ###

Highlights (details see below)
  * User-defined key shortcuts.

  * Catch-up with escape control sequences recently introduced by xterm.
  * Tweaks and fixes for window manipulation and other escape sequences.
  * Generic options to suppress various attributes and feature sets.
  * HTML Screen Dump function.

  * Optionally scale window to aspect ratio of background image.
  * Mouse handling enhancements.
  * Always allow switching scrollbar; do not switch on terminal reset (like xterm).
  * Keyboard AltGr workaround for buggy TeamViewer.

Terminal features
  * Avoid overwriting last column by clear-to-end-of-line/screen in pending auto-wrap state (#781).
  * Fix maximize vertically/horizontally (preserve width/height) and restore properly.
  * Fixed CSI 5/6/9/10 t default handling, preventing accidental window modification.
  * Fixed CSI 13/19 t to report multi-monitor virtual screen size without padding (like xterm).
  * New CSI 13;2/14;2/15/16 t (xterm 332/333).
  * New CSI # {/} XTPUSHSGR and XTPOPSGR push/pop character attributes (xterm 334, extended).
  * New CSI 0 i print screen.
  * Keeping wrapped line selection copy together after window has been resized (~#82).
  * Terminal soft reset resets cursor blinking (xterm 334).
  * DECSET 66 for application keypad.
  * Added DECSCPP and DECSLPP to DECRQSS (xterm 334).
  * Generic option to suppress character attributes (#468, ~#478, #777, ~#459).
  * Generic option to suppress DEC private mode switching.
  * Generic options to suppress window operations and configuration commands.
  * Optional filtering of pasted text (#768).

Keyboard handling
  * User-defined key shortcuts and function keys (#705, #602, #645, #399, #252, ~#726, ~#524, ~#451, ~#523).
  * Workaround for buggy TeamViewer (#783).
  * Guarding Alt handling from AltGr detection (#790).
  * VT220 keyboard toggle function for extended menu and user-defined keys.

Window handling
  * When suppressing focus-in mouse click event, also suppress the mouse release event (#782, #717).
  * After suppressing focus-in mouse click event, avoid subsequent false double-click report (#717).
  * Elastic mouse text selection includes only characters dragged more than halfway (#308).
  * Clearing hover highlighting as appropriate.
  * Tweaked selection size hint positioning (#660).
  * Optionally scale window to aspect ratio of background image (#18, #666).
  * Always allow switching scrollbar; do not switch on terminal reset (like xterm).

Desktop / taskbar integration
  * Partially withdrawn 2.9.0 patch to keep WSL windows together in Windows task bar (mintty/wsltty#96).
  * AppID (to group taskbar icons) is derived from WSL distro only with setting AppID=@ (#784, ~mintty/wsltty#96).

Configuration
  * New tool option `mintheme --save` to save theme in config file (#794).
  * Option ElasticMouse=true to not select characters only slightly touched (#308).
  * Option KeyFunctions for user-defined shortcuts and function keys (#705, #602, #645, #399, #252, ~#726, ~#524, ~#451, ~#523).
  * Special option setting AppID=@ to derive taskbar grouping implicitly from WSL distro name (#784, ~mintty/wsltty#96).
  * Support for Windows pathnames in background filenames (#18, #666).
  * Option to scale window to aspect ratio of background image (#18, #666).
  * New option value -RW to list installed WSL distributions and properties.
  * Option CtrlAltDelayAltGr for relaxed AltGr detection (#783).
  * Option SuppressSGR to suppress character attributes (#468, ~#478, #777, ~#459).
  * Option SuppressDEC to suppress DEC private mode switching.
  * Option SuppressWIN to suppress window operations.
  * Option SuppressOSC to suppress window configuration commands (~#385).
  * Option FilterPasteControls to filter pasted text (#768).

Other
  * HTML Screen Dump function (extended context menu or escape sequence).
  * Detect cygwin version for handling of @cjkwide locale modifier.
  * For illegal encoding, use REPLACEMENT CHARACTER if available.
  * Withdrawn Wyse cursor style modes SM 33/34 (#787).

### 2.9.0 (1 July 2018) ###

Highlights (details see below)
  * Background image and texture support.
  * Enhanced multi-monitor DPI handling.
  * Underline styles and colours, CMYK colour specifications.
  * Outer scrollbar mode, xterm-compatible.
  * Enhanced text selection mouse support.
  * Enhanced quick window switching.
  * Tweaked start error handling.
  * Tweaked WSL support.

Character attributes and rendering
  * Support for colon-separated SGR sub-parameters (ISO/IEC 8613-6) (xterm 282):
  * SGR escapes for ISO/IEC 8613-6 RGB, CMY, CMYK, and indexed colour formats.
  * DECRQSS uses SGR sub-parameters for colour specifications (xterm 331).
  * SGR 4:1...4:5 for underline styles solid, double, wavy, dotted, dashed.
  * SGR 58/59 for underline colour (kitty, iTerm2).
  * Fixed (almost) selection highlighting of emojis.
  * Drawing Unicode Block Elements which are broken in many fonts (#264).
  * Fixed initial bold as font suppression glitch (mintty/wsltty#103).

Window control
  * DECSET 30 to enable/disable outer scrollbar (like xterm) (#159, ~#262).
  * DECSET 1046 enables/disables alternate screen switching (xterm 331).
  * Cursor style modes DECSET 12 (AT&T 610) (xterm 331) and SM 33/34 (Wyse).

Scrollbar
  * Fixed scrollbar toggle (Ctrl+Shift+O) from forcing scrollbar to the right.
  * Preventing font zooming after scrollbar toggle (Ctrl+Shift+O).

Window layout
  * Background image or texture support (#18, #666).

Multi-monitor support
  * Using Windows DPI handling V2, avoiding fluttering (#774, #470, #492, ~#566, ~#547).

Text selection
  * Drag-and-drop selection after focus click enabled by combined cell and time distance (#717).
  * Only suppress focus-click selection if focus reporting disabled or mouse reporting not effective (#717).
  * Ctrl+mouse-move hovering underlines URLs and filenames (#173).
  * Selection highlighting can also indicate selection size (#660).

Window handling
  * Revised Ctrl+(Shift+)Tab window switching (#773).
  * Ctrl+Ctrl+(Shift+)Tab for window switching including iconized windows (#735).
  * Win+Shift move coupling of tab sets if SessionGeomSync ≥ 2 (#600, #699).
  * Keeping WSL windows together in Windows task bar (mintty/wsltty#96).

Configuration
  * CMY(K) colour specifications in OSC sequences and config file.
  * Option Background and OSC 11 to set background image or texture (#18, #666).
  * Option SelectionShowSize to enable selection size indication (#660).

Themes management
  * Fixed syntax error in `mintheme` tool (#764).
  * Option `mintheme -d -q` for decimal colour format (#718).
  * Supporting `mintheme` tool on WSL.
  * Options `mintheme -p` and `mintheme -t` to set background picture or texture.
  * Options `mintheme -s` and `mintheme -S` for theme visualization and comparison.

Other
  * Updated Emoji data and built-in width data to Unicode 11.0.
  * Enabled OSC 7 current directory injection for Ctrl+click in WSL (mintty/wsltty#104, mintty/wsltty#19).
  * Reporting start error (exit status 255) (#745).
  * Changed start error exit code from 255 to 126 (#745).
  * Fixed slowdown of Character Info mode if Unicode data are not deployed.
  * Option --WSLmode to tune behaviour for WSL distro but not launch (mintty/wsltty#99).
  * WSLtty appx mode triggered statically or dynamically (mintty/wsltty.appx#3).
  * Clarified additional configuration requirements for option TaskCommands in the manual.

### 2.8.5 (14 Apr 2018) ###

Character handling and rendering
  * CJK brackets are expanded if needed to ensure their symmetry (#756).
  * Support of emoji style text attribute SGR 51/52 (mintty/wsltty#82, #600).
  * Fixed emoji display variation selector handling.
  * Character Info displays emoji sequence short names (mintty/wsltty#82, #600).
  * Reverted Sixel colour registers patch that could fail Sixel display or even stall mintty window (#740, mintty/wsltty#90).

Input
  * Reenabled Ctrl+key escape sequences (#743).
  * Input optionally keeps selection highlighting (ClearSelectionOnInput=false) (#222).

Mouse handling
  * Reenabled drag-and-drop text selection on focussing at a threshold (~#717).

Configuration
  * Alt+F2 and mintty --dir=... stay in selected dir even in login mode (#500, #744).
  * Fixed option --dir=...
  * Context menu configuration (MenuMenu etc): new flags 'x' and 'u' (#755).
  * More layout-tolerant colour value syntax (#758).
  * Invocation as wsl*[-distro].exe implies a --WSL[=distro] parameter (mintty/wsltty#63).
  * Added missing option `mintheme --list`.
  * New mintheme options --file (#762) and --query.
  * New option ClearSelectionOnInput=false disables selection highlight clearing on input (#222).

Window handling
  * Clear selection when clipboard content updated (#742).
  * Skip refresh after colour setting if nothing changed (e.g. by prompt).

Desktop integration
  * WSL mount point configuration (/etc/wsl.conf or fstab) is considered in path conversion for open/paste (mintty/wsltty#91).
  * Workaround for ConEmu WM_MOUSEACTIVATE bug (#724).

Documentation
  * Terminal multiplexer configuration: added tmux (#757).
  * Search bar: activated also from context menu (#753).

### 2.8.4 (10 Feb 2018) ###

Character handling and rendering
  * Emoji support (mintty/wsltty#82, #600, ~#671, ~#430).
  * Fixed double-width handling within right-to-left text.

Window handling
  * Ctrl+Alt+mouse-click/drag moves the window (#729).
  * Apply Virtual Tab position catching also on Alt+F2 (#699).
  * Workaround for Windows failing to consider the taskbar properly when maximizing with no or frame-only border (#732).
  * Workaround for caption-less window exceeding borders (#733) on Windows 10.

Mouse handling
  * More selective suppression of mouse action on focus-click (mintty/wsltty#88, #717).
  * Fixed mouse function in search bar (#85), broken since 2.8.2 (#717).

Desktop integration
  * Support for taskbar "Tasks" list ("jump list") (#290).
  * Session launcher icons (#699).

Configuration
  * Options Emojis, EmojiPlacement to configure emojis style and display.
  * Option TaskCommands to configure a taskbar icon task list ("jump list").
  * Limit tweaking of empty shortcut start dir to Start menu case (#737).

### 2.8.3 (8 Jan 2018) ###

Character rendering
  * Fixed DEC REP for (self-drawn) VT100 graphics and non-BMP (~#634).
  * Tweaked output handling for ligature support (#601).
  * Support left cell overhang (for italics).
  * Fixed italic garbage (#725).

Configuration
  * Handling empty working directory (if started from Windows shortcut) for log file.

Window handling
  * Session switcher restores window only if it was iconic (mintty/wsltty#80).
  * Workaround for ConEmu focus incompatibility (#724).

### 2.8.2 (17 Dec 2017) ###

Window and session control
  * Transparent session switcher icons (#699).
  * Avoid blocking when switching to other window which is blocked/suspended.
  * Prevent initial bogus font zooming (#708).
  * Prevent accidental text selection on mouse-click window activation (#717).
  * Terminal Break available in extended context menu (#716).
  * Terminal Break assignable to Break key (option Key_Break) (#716).

Configuration
  * Support ~/ prefix for config files in SessionCommands configuration for session launcher.
  * Support ~/ prefix for logfiles, especially for usage in config files.
  * With --WSL, let option "-" request login shell.
  * Icon determined via --WSL shall not override icon taken from shortcut.
  * Option ShortLongOpts enables single-dash named options on command line (#600, requested by Brian Inglis).
  * Convenience xterm-style command line options: -fn, -fs, -geometry, -fg, -bg, ...

Character rendering and text attribute handling
  * Caching ambiguous character width for speed-up (#712).
  * Revised weight attribute handling, esp. bold display (thanks to avih, #714, #710).
  * Distinguishing ANSI colours 0..15 from palette colours 0..15 like xterm (~#714).
  * OSC 6 can enable/disable bold foreground colour (xterm).
  * Tweaked brightened bold to never decrease the text colour contrast.
  * Terminal Reset also resets dynamic bold foreground colour.
  * Fixed non-BMP italic display.
  * Proper support of true colour attribute for Copy as rich text (thanks to avih, #710).
  * Colour setting sequence OSC 4 limited to the defined palette range (~#710).
  * Allowing minor font size deviation of bold font.
  * Underlay manual underline and overline behind text.
  * Option BoldAsRainbowSparkles.

Other
  * Bundling selected theme files with the package (#711).
  * Support Alt+F2 in same directory (after OSC 7) for WSL.
  * MINTTY_PID in UserCommands: support to terminate foreground process (#716).
  * Fixed bug when copying true-colour text as rich text (thanks to avih).
  * Prevent HOME from being propagated back to Windows applications if called from WSL (mintty/wsltty#76).

### 2.8.1 (31 Oct 2017) ###

Character display
  * Option Charwidth for built-in Unicode width or ambiguous wide mode (#88, #671).

Window and session control
  * Virtual Tabs: additional SessionGeomSync levels (#699).
  * Window icons can optionally be shown in session switcher (#699).
  * Restore window frame when leaving fullscreen mode via escape sequence.
  * Tweaked full-screen handling in session switcher and launcher (#600).

Pathname handling
  * Relative pathname opening considers interactive working directory (~mintty/wsltty#19).
  * Pathname opening accepts escaped space or embedding quotes (~mintty/wsltty#19).
  * Relative pathname opening from WSL ignores improper directories (mintty/wsltty#19).

WSL support
  * Made WSL parameter optional: --WSL.
  * Option --WSL supports legacy-only installation "Bash on Windows" (mintty/wsltty#64).

### 2.8.0 (22 Oct 2017) ###

Window and session control
  * Virtual Tabs (#8, #600).
  * Options SessionCommands, SessionGeomSync, Menu*.

Configuration
  * New option BellFlashStyle (#676) and more moderate default flash style.
  * Option -l / --log implies Logging=yes.
  * `mintheme` command-line theme switcher (#685).
  * Preventing @cjknarrow locale modifier for WSL (#686).
  * Fixed Alt+F2 in same dir (with OSC 701) not to expand symbolic links.
  * Options HighlightBackgroundColour and HighlightForegroundColour.

WSL support
  * Option --WSL= to run WSL session (mintty/wsltty#52, mintty/wsltty#59, ~mintty/wsltty#60).
  * Option -~ to start in user's home directory (~mintty/wsltty#3).
  * Update availability check for wsltty build refers to wsltty version (mintty/wsltty#20).
  * Fixed rootfs handling in pathname conversion (mintty/wsltty#19).

Terminal control sequences
  * Fixed status string DECRQSS (#689, #690, mintty/wsltty#55, vim/vim#2003).
  * DEC Locator mouse mode (facilitating pixel-based position).
  * DECRQM request mode (to reach VT300 conformance level).
  * Save/Restore DEC Private Mode (DECSET) values (#267).
  * DECSTR soft terminal reset.
  * DSR DEC variant (cursor/printer status).
  * REP: repeat preceding character.
  * OSC 50: set/query font.
  * OSC 17/19/117/119: set/reset selection highlight colours.

Character display
  * Tweaked width expansion of ambiguous-width characters (#680, ~#638, ~#615).
  * Fixed notes on ambiguous-width handling with Locale parameter (~#686).
  * Legacy character set support: NRC, DEC Supplemental, GR invocation.
  * Fixed NRC single shift with attributes.
  * DEC Tech: tweaked √/Σ segments (hand-drawn), fixed double-size characters.
  * Fraktur font support (ANSI character attribute 20).

Keyboard and Mouse
  * Workaround for broken AltGr of Windows on-screen keyboard (#692).
  * 5-button mouse support.

Localization meanwhile available for (in alphabetical order of locale ids):
  * German, English (UK/US), Spanish, French, Japanese, Russian, Chinese.

### 2.7.9 (30 July 2017) ###

Character display
  * Fixed character attribute handling in scrollback buffer.
  * Fixed rendering of some combining characters by heuristic tweaking.
  * Avoid misplaced artefacts of combining doubles while moving cursor over them.
  * Limiting glyph width checking to symbol ranges to avoid performance penalty (~#615).

WSL support
  * Build option VERSION_SUFFIX to add package version indication (mintty/wsltty#35, mintty/wsltty#50).
  * WSL path conversion supports Store distribution packages (mintty/wsltty#52).

Other
  * Unicode 10.0 updates.
  * Avoid multiple reporting of font problems.
  * Fixed process list in Close prompt (if started from desktop).

### 2.7.8 (25 June 2017) ###

Font rendering
  * Support for alternative fonts as selected via ECMA-48 SGR codes 11...19.
  * Tweaked ambiguous width checking to not expand glyphs excessively (#615).

Terminal features
  * SGR 6 attribute "rapidly blinking" (ECMA-48).
  * Fixed Cursor Position Report (CPR) in Origin Mode (DECOM) (vttest 6-3).
  * DEC Technical character set support (approximating some segments).
  * G2/G3 character set designations and GL mapping (vttest 3-10 GL cases).
  * G2/G3 character set single shift selection (vttest 3-11).
  * Resetting invoked character set to G0 on terminal reset.
  * CHT (move right n tab positions) (vttest 11-5-4).
  * Completed Status String (DECRQSS) with missing attributes.

Window handling
  * Suppressing Windows-caused side effects of "resizing" to current size (#629).
  * Tweaked window-raising to top (previous #652) to not stick on top (mintty/wsltty#47, #667).
  * Reverted capturing of Shift+Ctrl+0 (~#233) to enable language switching (#663).
  * Capture key after leaving system menu with ESC; insert it and prevent beep.

Configuration
  * Reverted to stripping CR from most configuration strings (mintty/wsltty#46).

### 2.7.7 (20 May 2017) ###

Font rendering and display handling
  * Automatically disabling Uniscribe for ASCII-only chunks (mintty/wsltty#36).
  * Fixed bidi display while showing other screen with Bidi=1 (#592, ~#392, ~#605).

Window handling
  * Avoid being pushed behind other windows on Ctrl+TAB (#652).
  * Tweaks to stabilize initial window geometry (#629, #649).
  * Fixed broken size parameters maxwidth/maxheight.

Terminal handling
  * Option NoAltScreen disables the alternate screen (~#652).
  * Fixed delayed scroll marker highlighting (#569).
  * Manual: link to Wiki about console issues / winpty wrapper (#650).

Configuration
  * New option --configdir for config file and resource folders (mintty/winpty#30, mintty/winpty#40, mintty/winpty#38).
  * Always save to config file specified with -c/--config or --configdir (mintty/winpty#30, ~mintty/winpty#40).
  * Options: offer resources from subdirectories of all config directories (#639, #30, ~#38).
  * Configuration resource subdirectories are created as needed (#30, #38, ~#639).

Other
  * Fixed cleanup of environment variables after user command (#654).
  * Fixed usage of /tmp or (if read-only) alternatives (mintty/wsltty#30).
  * Wiki: removed link to buggy themes (#647).

### 2.7.6 (14 Apr 2017) ###

Font rendering and Screen handling
  * Supporting cell overhang of italics (#418, #152, #638).
  * Manual underline adds less thickness in bold mode (#641).
  * Consistent search highlighting while scrolling (#85).

Bidirectional rendering
  * Support for bidi implicit and override marks (validated) (#392, ~#605).
  * Support for bidi embedding marks (#392, ~#605).
  * Partial support for bidi isolate marks (#392, ~#605).
  * Option Bidi to disable bidi completely (=0) or only on alternate screen (=1) (#592, #392, ~#605).
  * Bidi can be disabled with CSI ?77096h (#592, #392, ~#605).
  * Bidi can be disabled per screen line with CSI ?7796h (#592, #392, ~#605).

Window handling
  * Fixed occasional hangup on Apply in font chooser (~#533, ?mintty/wsltty#27).
  * Workaround for broken Windows 10 window position reference (#629).
  * Fixed re-initialisation of colour chooser adjustment (#642).
  * Fixed Alt+F2 size cloning inconsistencies (#633).
  * Stick new window to current monitor with Ctrl+Shift+N and sysmenu-New (#649).

Configuration
  * Optional built-in inline font chooser, option FontMenu for font chooser configuration (?#533, ?mintty/wsltty#27).
  * Command line option -Rf to list installed fonts as used by mintty.
  * Colour scheme download drops dependency on `curl` tool (#193).

### 2.7.5 (11 Mar 2017) ###

Font handling / Text rendering
  * Changing default FontRender=uniscribe (#605, #573).
  * Zoom box drawing and some other characters to full cell size so they can connect to each other (#628).
  * Restricted glyph width scaling to support overhanging wide icons (#638).
  * Tweaked italic overhang clipping (#638).

Terminal
  * Fixed Backspace upper limit in Origin mode.
  * Added switching feature for Reverse Wraparound mode.
  * Reverse Wraparound is false by default (rather than fixed true before), to comply with xterm and terminfo.
  * Tweaked Wraparound and Backspace behaviour to comply with xterm and pass vttest 1.
  * Option OldWrapModes=true would restore previous wraparound behaviour.
  * Supporting OSC sequences 110/111/112 to reset foreground/background/cursor colour.

Search
  * Enabled matching of non-BMP characters (#85).
  * Case-insensitive matching (#636).

Tty and character set
  * Keeping termios flag IUTF8 in sync with locale/charset to support proper backspace behaviour of programs not using readline.

Drag and drop
  * Drag-and-drop pasting applies configurable pattern (#440).

Menu and hotkey functions
  * Scroll markers (#569), facilitating quick scroll to previous/next command prompt.
  * Optional extended context menu (with Ctrl).
  * Extended context menu functions Copy & Paste (#539), Clear Scrollback (#421).
  * Logging can be toggled from extended context menu (#342, ~#546, ~#279).
  * Character information mode, switchable from extended context menu.
  * User-defined commands in extended context menu (#100, #475).
  * Ctrl+Shift+T hotkey cycles transparency (#625).
  * Ctrl+Shift+T+cursor-block hotkeys tune transparency (#625).
  * Ctrl+Shift+P hotkey cycles cursor style.
  * Ctrl+Shift+O hotkey toggles scrollbar.

Configuration
  * Option to specify logfile but disable initial logging (Logging=no).
  * New drag-and-drop paste configuration DropCommands (#440).
  * New user commands configuration UserCommands (#100, #475).
  * New option OldWrapModes to restore previous wraparound behaviour.
  * Checking availability of mintty version update in Options dialog; adjust or disable with option CheckVersionUpdate (mintty/wsltty#20).
  * Options dialog can be scaled via special pseudo-localization text (#637).
  * Fixed localization of command-line messages (#637).

Options dialog
  * Extending font sample area to both sides, allowing longer font samples.
  * Fixed mangled position of colour chooser item (#626).
  * Unsqueezing colour chooser dialog to make more space for localized labels (#614, #611).

### 2.7.4 (29 Jan 2017) ###

Localization details:
  * Fixed localized Bell field contents.
  * Adapting Bell list contents from system localization.
  * Fixed unlocalized Colour chooser label "Basic colours:" and Font chooser initial font sample.
  * Fixed localized Colour chooser label "Basic colours:".
  * Fixed Colour chooser label "Custom colours:" (disappeared on refocussing).
  * Added localization of "Error" popup title.
  * Keeping button labels in reactivated message box.

Configuration and Terminal settings:
  * BellTaskbar setting is switchable by escape sequence CSI ?1042h (xterm).
  * New BellPopup setting, switchable by escape sequence CSI ?1043h (xterm).
  * Revised Bell section in Options menu.
  * New option FontSample.
  * Tweaking Font chooser dialog to widen font sample area.

Other:
  * Extended WSL link conversion (for link-click and link pasting) to non-/mnt paths (~#164, mintty/wsltty#19).
  * Fixed window popup (on escape sequence CSI 5t).
  * Allowed automatic font metrics adjustment to increase row spacing.

### 2.7.3 (23 Dec 2016) ###

Character display:
  * Tweaked surrogate handling to facilitate non-BMP display (#616).

Localization:
  * Persisting adaptation of font sample text.
  * Disambiguated translations in different context for "Paste", "Font", "Colour".
  * Sorted out en_GB vs. en_US.
  * Fixed localization of Options dialog window title.

### 2.7.2 (10 Dec 2016) ###

Localization:
  * Completion of localization support, covering font chooser and colour chooser (#537).
  * Hint in Theme field as feedback after dragging downloaded colour scheme.

Other:
  * Fix config dialog crash when trying to load resources from network drive (#610).

### 2.7.1 (4 Dec 2016) ###

Character display:
  * Uniscribe support for left-to-right text (~#573, ~#605, /#430), achieving:
  * Improved font fallback.
  * New option FontRender=uniscribe to enable Uniscribe support (#605, #573).
  * New option UnderlineManual to enforce manual lining with default colour (#604).
  * Support for distinct colour attributes for combining characters.
  * Ensuring font underlining of separately drawn combined characters.

Localization:
  * Fallback from region-specific to generic translation ("fr_FR" -> "fr") (#537).
  * Windows user language as an additional option for language selection (#537).
  * Enabled localization of Options tree menu labels (#537).
  * Enabled localization of system menu standard items (#537).
  * Support reverse-localization of Windows-localized menu entries with Language=en.
  * Simplified menu item localization (#537).
  * Localization of message boxes (#537).
  * Preserving system menu icons.
  * Avoiding artefacts with non-ASCII labels of Options dialog elements.

Keyboard:
  * Enabled Compose key with Shift in modifyOtherKeys mode.

Other:
  * Suppressing repeated font error messages.
  * Enabled link-click in wsltty (~#164).

### 2.7.0 (13 Nov 2016) ###

Character display:
  * Check for misplaced underline by looking for underline only in descender section to decide whether to draw it ourselves (#604).
  * New option UnderlineColour to set colour for all lining modes and enforce manual underlining (~#604).

Terminal:
  * Compose key.
  * Workaround to enable Shift+space in modifyOtherKeys mode.
  * Avoiding multiple device attribute reports (#606).
  * Dynamic switching escape sequences for wide Indic and Extra characters (#553).

Configuration:
  * User interface localization (#537, #1).
  * New option value -Rm to report system's monitor configuration (/#599).
  * New option --wsl to enable WSL feature adaptations (mintty/wsltty/#8).
  * Manual: fixed description of copy/paste shortcuts and refer to option CtrlExchangeShift (#602, #524).
  * Case-insensitive lookup for X11 color names (#600).
  * Dropped experimental options WideIndic and WideExtra in favour of dynamic switching escape sequences (/#553).
  * Detection and graceful handling of corrupt font installation.

### 2.6.2 (9 Oct 2016) ###

Terminal geometry:
  * Enabling initial scrollbar (as configured) on Windows 10 (#597).
  * Adapting search bar height to font height (zooming, DPI scaling) (#85, #233).
  * Fixed zoom anomaly (e.g. Shift+Ctrl+mouse) with search bar (#85, #233).
  * Considering search bar (#85) for pixel size setting/reporting.

DPI scaling:
  * Tweaked DPI scaling of window decoration, considering option HandleDPI.
  * Tuning window size according to DPI again.
  * Workaround for Windows non-client DPI scaling ignoring window name for title bar.

Character display:
  * Support for "combining double" characters that cover two base characters (~#553).
  * Expanding characters as appropriate for wide display (#123, /#570).
  * Option --nobidi / --nortl to disable bidi mode (right-to-left support) (#592).

Sixel image display:
  * Changed non-graphic display substitute for Sixel image to space to reduce flickering (~#587).
  * Changed non-graphic clipboard substitute for Sixel image to configurable string, default space (#587).
  * Fixed Sixel colour registers handling (#593), thanks to Hayaki Saito.
  * Stuffed Sixel buffer leak (#595).
  * Fixed slow scrolling with Sixel images (Windows resource leak, #594).

Configuration:
  * New command line option --dir to change the initial working directory (#558, #58).
  * Option --nobidi / --nortl to disable bidi mode (right-to-left support) (#592).
  * Revised help text (-H).

### 2.6.1 (18 Sep 2016) ###

  * Tweaked DPI scaling to avoid terminal resizing on font selection (~#492).
  * Enabling auto-scaling of window decoration (#547, #517, thanks to Jason Mansour #588).

  * Option AllowSetSelection to enable OSC 52 control sequence to set clipboard for pasting (#258, thanks to kangjianbin).
  * Tweaks for Sixel graphics feature.
  * Tweaked underscore cursor position, considering row spacing (#589).

  * Retaining config file comments (#574).

### 2.6.0 (8 Sep 2016) ###

  * Sixel graphics support (#572, thanks to Hayaki Saito).
  * VT340 terminal ID configuration option (corresponding to Sixel feature).
  * Fixed primary DA response, depending on configured terminal ID.

### 2.5.1 (6 Sep 2016) ###

  * Fixed disappearing VT100 graphic characters when font size too small (#578).
  * Fixed font scaling behaviour in Windows 7 and XP (#492).
  * Fixed DPI scaling of Options menu (#492).
  * Fixed font description formatting in Options menu.

### 2.5.0 (23 Aug 2016) ###

  * Revised DPI handling (#470; #492, #487); always consider individual monitor DPI.

### 2.4.3 (23 Aug 2016) ###

  * Fixed trails when moving other window over mintty under certain conditions (#576).
  * Fixed format of double child creation error message.

### 2.4.2 (27 Jul 2016) ###

  * Reverted change (#123) that spoiled wide character display (#570).
  * Changed bell to make sound by default (like xterm) (#568).

### 2.4.1 (23 Jul 2016) ###

Window handling:
  * Tweaked and guarding DPI change handling (#566, #470).

Text display:
  * Combined enhancement for rendering of combined characters (#565).
  * Enforcing font selection at uniform size, to ensure using bold font.
  * Restored self-drawn, overstriking bold mode (#567).
  * Stretching narrow characters that are expected wide (#123).
  * Option WideExtra for double-width display of extra wide/long Unicode characters (experimental).

Terminal:
  * Fixed ESC[14t to report same pixel size as xterm would (without padding).

### 2.4.0 (9 Jul 2016) ###

Configuration:
  * Drag-and-drop for Theme and Wave sound file name configuration (#193).
  * Link to Color Scheme Configurator in Options menu (#193).
  * Drag-and-drop and Store from Color Scheme Configurator (#193).
  * Drag-and-drop and Store from theme file on the web (#193).
  * Loading theme and bell sound resources also from XDG default config dir (#525), Windows AppData dir, or /usr/share/mintty.
  * Enabling cygwin path style for wave file configuration.
  * Size option -s is not persisted in config file on Save.

Text display:
  * Handling low-contrast of configured cursor colours more gracefully (#548).
  * Tweaked manual underline to scale with font and avoid scroll glitches.
  * Manual drawing of VT100 line drawing graphics (#130, ~#551).
  * Fixed right-to-left mirroring of '('.
  * Not combining characters to unexisting glyphs.
  * Preventing mangled digit rendering if surrounded by certain scripts (~#285).
  * Preventing mangled combining characters by drawing them separately (~#553, #295).
  * Option WideIndic for double-width display of wide Indic characters (#553) (experimental).

Window handling:
  * Tweaked DPI changes to stabilize roundtrips of font and window size (#470).
  * Option HandleDPI=false to disable handling of DPI changes (#547, #492).
  * Heuristic attempt to stabilize font size roundtrips after fullscreen.

### 2.3.7 (22 May 2016) ###

Text display:
  * Fixed double-width line display (#551).
  * Enabled non-BMP right-to-left display.
  * Fixed italic font height and underline placement with option RowSpacing.
  * Tweaked underline handling and positioning (~#551).
  * Workaround for glyph confusion (#285).

Terminal input / Keyboard:
  * Application keypad modified keys send sequences like in xterm (#506).
  * Option CtrlExchangeShiftExchange exchanges Control characters with Ctrl+Shift shortcuts (#524).
  * Fix drag-and-drop file/folder to also honour bracketed paste mode (#440).

Terminal control:
  * Escape sequence OSC 7 with empty string restores the default behaviour.
  * Shortcut key Shift+Ctrl+A for "Select All".

Configuration:
  * Loading config also from XDG default config dir (#525) or Windows AppData dir (#201).
  * Option OpeningClicks configures whether documents are opened on single/double/triple Ctrl+mouse-click (#545).

### 2.3.6 (30 Apr 2016) ###

Font configuration:
  * Tweaked font weight selection (#520) to enforce selected boldness.
  * Showing font warning on stderr rather than message box if possible (#527).
  * Fixed font selection menu on Windows XP.
  * Limit automatic row spacing adjustment to negative case, in order to prevent breaking box characters.

Keyboard:
  * AltGr+space menu invocation may be overridden by keyboard layout (#542).

Other:
  * Escape sequence OSC 7 informs terminal of current working directory, to be cloned on Alt+F2.
  * Log file name supports strftime pattern (#546).

### 2.3.5 (4 Apr 2016) ###

  * Reenable combined bold as font and colour for some mono-weight fonts (#536).

### 2.3.4 (3 Apr 2016) ###

  * Workaround for suspected compilation problem causing crash after daemonizing (#530).

### 2.3.3 (21 Mar 2016) ###

  * Limit font warning to fonts with neither ANSI nor system locale support (#527).
  * Also include OEM/SYMBOL fonts with option -o ShowHiddenFonts=yes.

### 2.3.2 (20 Mar 2016) ###

  * Fixed 64 bit adaptation for weird Windows function.

### 2.3.1 (20 Mar 2016) ###

  * Fixed character set support warning for non-Western system locales (#527).
  * Fixed log output to stdout (-l "-") (#528).

### 2.2.4 (20 Mar 2016) ###

Font configuration:
  * Mintty adjusts row spacing according to the font metrics, to compensate for tight or tall spacing of some fonts (e.g. Consolas, FreeMono, Monaco). (The RowSpacing value is added to that.)
  * Adjusting font weight selection to available font weights (#520).
  * New option FontWeight supports more specific font weight selection (#520).
  * Font selection menu has its own Apply button.
  * Fonts with name problems (long names, #507) can be selected with the Apply button.
  * Warnings for font not found or not supporting ANSI character set.
  * Excluding fonts with OEM or SYMBOL charset from font selection menu.
  * Excluding vertical fonts from font selection menu.
  * New option ShowHiddenFonts to offer monospace fonts marked to Hide in the menu.
  * Unicode-enabled Font setting (so e.g. mintty -o Font=Sütterlin works).

Themes, Configuration, and Options menu:
  * Colour schemes: New option ThemeFile, configuration also in Options menu (~#193).
  * Configuration of .wav bell sounds (option BellFile, #369) in Options menu.
  * Resource directory $HOME/.mintty for theme and bell files.
  * Fixed -o settings to also be saved when changed in Options menu.

Keyboard and mouse features:
  * Workaround for occasional Alt state inconsistencies after window focus changes (#519).
  * Compose key on wiki pages: replace AllChars with WinCompose.
  * Opening marked "www." addresses also without "http:" prefix (#345).

Start and error handling:
  * Fixed -C/--loadconfig to not overwrite common options in main config file.
  * Fixed format substitution for log file in case of excess % conversions.
  * Report full pathname of log file if creation fails.
  * Improved and fixed format of child creation error messages.
  * Improved reporting failed icon loading with non-ANSI icon filenames.

### 2.2.3 (7 Feb 2016) ###

Desktop integration:
  * Deriving icon from shortcut (#471, ~#420, ~~#486).
  * New option -D with impact to shortcut key behaviour (~#499).
  * Not failing to start if daemonizing fails (#493).
  * Shift+Alt+F2 clones the window at the configured size.
  * Fixed Alt+F10 to restore the configured size even in Alt+F2-cloned window.
  * Fixed Shift+Alt+F10 to restore both window size and font size.
  * Limiting drag-and-drop pasting to actual terminal window, not Options menu.
  * Handling changing window frame geometry (e.g. Personalization) (~#429).

Terminal layout:
  * Not switching transparency when entering search bar (#497, thanks to Kai).
  * New option Padding (#511).
  * Adjusting window to font change (#429) and sending notification if enabled.

Keyboard:
  * Extended special key redefinition options (#494).
  * Fixed broken Pause/Break key defaults (#515), tweaked configuration.

Bell:
  * Added configuration option BellFile (#369) to play wav sounds.
  * Enhanced bell sound selection in Options menu; option BellSound obsolete.
  * Test button for bell sound in Options menu.

Configuration and Printing:
  * Support process ID substitution (for %d) in log file name.
  * Support for Unicode configuration strings and Unicode printer names.
  * Support for printing fixed and tweaked, using terminal character set.
  * Indicating Printing status in window title, in case of pseudo-blocking.

### 2.2.2 (12 Nov 2015) ###

  * Tweaked taskbar grouping behaviour (#486).
  * Advice on avoiding trouble with taskbar grouping and icon consistence in manual page and wiki Tips page (#420, #486, ~#471).
  * Fixed New window option from window title menu on multi-monitor systems (#491).
  * Fixed start on other monitor in Windows 10 / MinGW (#489, thanks to rupor).
  * Guarding against escape sequence parameter overflow (~#490, thanks to Iwamoto Kouichi).

### 2.2.1 (3 Nov 2015) ###

Major New Search Feature (thanks to Kai (twitter:@sixhundredns)):
  * Search scrollback buffer (#85); shortcuts Alt+F3 or Shift+Ctrl+H; configuration options.

Window placement and Multi-Monitor support:
  * Option -p @N to select monitor (#288).
  * Interactive feature to tweak Alt+F2 to select monitor.
  * Options -p right and -p bottom to align window position (#288).
  * Option -s accepts special values "maxwidth" or "maxheight" (#171).
  * Per-monitor DPI support (#470, thanks to Takashi Kawasaki).
  * Fixed initial terminal size if reduced border is specified (#7).
  * Trying to enforce initial focus (#57).
  * New option ZoomFontWithWindow to disable Shift-coupled font-with-window zooming (#476).
  * Accepting xterm-compatible syntax in size parameter, like -s 80x24.

Keyboard input:
  * Supporting layout-specified key input for all cases (#483, thanks to maxime1986).
  * Combining accented characters that are not supported by Windows (#484).
  * Application control key mode (#405).
  * Tweaked/disabled shift-coupled window-with-font zooming on some keys; thus:
  * Reenabled Ctrl+_ (if _ is Shift+- on keyboard layout).
  * Avoiding inadvertent window-with-font zooming if "+" is a shifted key.

Bold attribute handling:
  * Tweaked smart brightening (for BoldAsColour), considering contrast to both normal colour and background.
  * Support BoldAsColour without BoldAsFont for plain text (#468).
  * New option BoldColour (#468, #478).
  * Not enforcing bold-overstriking if bold colour explicitly redefined (#468, #478).
  * New xterm OSC sequences (5;0;rgb/105;0) to define/reset colour for bold attribute (#468).

Other terminal features:
  * Fixed character operations beyond terminal width (#480).
  * Supporting X11 color names for colour specifications in OSC sequences.
  * Supporting xterm sequences to maximize window vertically/horizontally (#394).
  * New private OSC sequence to copy the window title to the clipboard (#303).

Configuration:
  * Changed action buttons in Options dialog; Apply does not save changes.
  * Added some Options menu configuration items (for previously introduced new options, thanks for the pattern to James Darnley #384).
  * New option -C to load additional configuration file without saving to it, particularly for use with colour schemes.
  * Supporting X11 color names for colour specifications in options.
  * New option -R to report window geometry on exit (~#477).
  * Optional Windows taskbar integration (#471, thanks to Johannes Schindelin).
  * Not inhibiting size options in nested invocation from Alt+F2.

### 2.1.5 (19 Aug 2015) ###
  * Guard Shift+Ctrl+0 detection (#233) to avoid interference with keyboard switchers (#472).
  * Basic fixes for displaying child process list on exit confirmation (#448).

### 2.1.4 (6 Aug 2015) ###
  * Not zooming font on Shift+Windows shortcuts (#467), by heuristic analysis of Windows messages.
  * Not daemonizing if started from ConEmu (#466), by heuristic check of $ConEmuPID.

### 2.1.3 (30 July 2015) ###
  * With position option, "centre" or "center" can be specified (#208).
  * Enabled new character attributes strikeout, doubly-underlined, overlined.

Zooming:
  * Control-middle-mouse click resets zooming, complementing Control-mouse-wheel scroll in analogy to Control-+/-/0.
  * New option ZoomMouse=off to disable mouse-wheel zooming.
  * Enabled Shift+Ctrl+0 to reset zooming for font and window (#233).

Fixes:
  * Fixed crash after conditional daemonizing (#464, #465).
  * Apply daemonizing for cloned window (Alt+F2) to avoid zombie process (thanks to Paul Townsend).
  * Made conditional daemonizing the default again.
  * New option -d to disable daemonizing as a workaround just in case...

### 2.1.2 (24 July 2015) ###
  * Detach from caller's terminal only with option -D.

### 2.1.1 (23 July 2015) ###
  * Tweaked Ctrl+TAB to not put current window into the background (~ #260).
  * Ctrl+click spawning: Syncing environment to Windows to avoid dropping environment after Alt+F2 (#360).
  * Trying to keep window position within monitor work area on resizing (#79).
  * Tweaked to exit while background process is running (#319).
  * Trying to detach from caller's terminal in order to not suppress signals when started from Cygwin console.
  * Shift syncs font and window zooming (#233, #204).
  * Added termination indicator config options ExitWrite and ExitTitle (#437).
  * Added options -B frame and -B void to reduce window border (#7).
  * Displaying child process list on exit confirmation (#448).

### 2.0.3 (11 July 2015) ###
  * Implemented (but not enabled) character attributes doubly-underlined and overlined; disabled overlayed "non-bold" attribute (SGR 21).
  * Desktop entry for xdg menu.
  * Tweaked boldening to cover all cases and options properly (~ #459).
  * Disabled obscure character encoding mode 12 (from Linux console) which would render ASCII codes as Greek.

### 2.0.2 (5 July 2015) ###
  * Hotfix Alt+F2.

### 2.0.1 (1 July 2015) ###
Display:
  * Fixed bold display by overstriking if BoldAsFont unset.
  * Implemented and enabled character attribute italic (#418, #152).
  * Implemented character attribute strikeout; disabled due to missing bit.
  * True Colour support (#431) (using ESC [ 38;2;r;g;b m).

Window:
  * Alt+F2 creates new window of same size as current one (#275).
  * Option -T to set unchangeable window title (#385).
  * Fixed CSI 10;2t to toggle full-screen mode.
  * Fixed CSI 9;2t and 9;3t to do nothing like in xterm.
  * Fixed CSI 8;...t handling of default and zero values (issue #408).
  * Reporting mode for font changes (issue #335).

Keyboard:
  * Option DeleteSendsDEL and associated switching sequence CSI ? 1037 h/l to switch keypad Del key sending DEL or Remove (#406).
  * Treating AltGr similarly to Shift in modifyOtherKeys mode (issue #272).
  * Options Break and Pause to configure mappings for these keys (#399).

Other Options:
  * Added off/on as alternative Boolean configuration values.
  * Added configuration options BellType, BellFreq, BellLen (~ #369).
  * Option HideMouse=false disables mouse cursor hiding on keyboard input (#403).
  * Configuration option WordCharsExcl to exclude characters from word selection (#450).
  * Configuration file .minttyrc can contain empty lines and comment lines starting with #.
  * Option MiddleClickAction=void disables mouse-middle-click pasting (#384).
  * Option to simulate Enter/Return with mouse-click (#425).
  * Documented RowSpacing/ColSpacing; tweaked to distribute padding evenly.

Other:
  * Fixed fonts array index limit https://cygwin.com/ml/cygwin/2015-02/msg00415.html
  * Added MSYS setup hint (#426).

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
  * Tweaked double-click selection and Ctrl+click opening to recognise URLs with parameters.
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
  * The mintty-0.4 mousewheel keycodes can be enabled and disabled using the new control sequences `\e[?7787h` and `\e[?7787l`. These can be used to distinguish the mousewheel from the cursor keys without enabling full mouse reporting.

Locales and charsets:
  * The 'Codepage' option is now called 'Character set', and there's a new 'Locale' option for language and territory.
  * If no locale is set in the options, mintty uses the locale specified via the environment variables LC\_ALL, LC\_CTYPE, or LANG.
  * If the locale option is set, the character set is appended to it and the LANG variable set accordingly and LC\_ALL and LC\_CTYPE are cleared.
  * The @cjknarrow locale modifier is automatically appended to LANG if an ambiguous-narrow font is used with an East Asian locale. (See also http://www.cygwin.com/1.7/cygwin-ug-net/setup-locale.html)
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
  * Dropped support for C1 control characters (i.e. 0x80 to 0x9F). This is a VT220 feature, whereas MinTTY only claims to be a VT100 via its "primary device attribute" string. Removing support makes Cygwin's /bin/ascii utility work correctly with any 8-bit codepage and decreases the likelihood of accidental binary output messing up the terminal settings. Rxvt doesn't support the C1 control characters either, but xterm does. Please let me know of any applications where this incompatibility causes problems.
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
  * Closing on Alt+F4 can be disabled (on the Window panel).
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