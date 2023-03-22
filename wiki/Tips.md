
## Configuring mintty ##

Mintty supports a number of common places to look for and save its 
configuration and resources.

For its configuration, it reads configuration files in this order:
* `/etc/minttyrc`
* `$APPDATA/mintty/config`
* `~/.config/mintty/config`
* `~/.minttyrc`

For resource files to configure a colour scheme, 
wave file for the bell character, localization files, emoji graphics, 
dynamic fonts, pointer shapes, it looks for subfolders 
`themes`, `sounds`, `lang`, `emojis`, `fonts`, `pointers`, respectively, 
in the directories
* `~/.mintty`
* `~/.config/mintty`
* `$APPDATA/mintty`
* `/usr/share/mintty`

The ```~/.config/mintty``` folder is the XDG default base directory.
The ```$APPDATA/mintty``` folder is especially useful to share common configuration 
for various installations of mintty (e.g. Cygwin 32/64 Bit, MSYS, Git Bash, WSL).
An additional directory for a configuration file and configuration resources 
can be given with command-line parameter ```--configdir```.


## Desktop integration ##

### Using desktop shortcuts to start mintty ###

The Cygwin [setup.exe](http://cygwin.com/setup.exe) package for mintty 
installs a shortcut in the Windows start menu under _All Programs/Cygwin_.
It starts mintty with a ‘-’ (i.e. a single dash) as its only argument, 
which tells it to invoke the user’s default shell as a login shell.

Shortcuts are also a convenient way to start mintty with additional options and different commands. 
For example, shortcuts for access to remote machines can be created by 
invoking **[ssh](http://www.openssh.com)**. The command simply needs 
to be appended to the target field of the shortcut’s properties:

> Target: `C:\Cygwin64\bin\mintty.exe /bin/ssh server`

The **[cygutils](http://www.cygwin.com/cygwin-ug-net/using-effectively.html#using-cygutils)** package 
provides the **mkshortcut** utility for creating shortcuts from the command line. 
See its manual page for details.

### Smart login mode ###

If invoked from a Windows shortcut (desktop or start menu), mintty starts 
the shell in login mode implicitly (since mintty 3.4.7) unless disabled 
with option `LoginFromShortcut`.

### Hotkey / Windows Shortcut key ###

In a Windows shortcut (desktop or Start menu), a "Shortcut key" with 
modifiers can be defined as a system hotkey to start an application or 
bring it to the front.

To have a new instance started with every usage of the hotkey, use the 
command-line option ```-D``` for mintty in the shortcut target.

### Hotkey / "quake mode" ###

Mintty detects if activated via hotkey and will use the same hotkey to 
minimize itself in turn, unless inhibited by shortcut override mode. 
The preferred appearance of the hotkey-activated terminal window can 
further be customized with options in the shortcut, e.g.

> `C:\Cygwin64\bin\mintty.exe --pos top --size maxwidth -`

### Taskbar icons ###

In a Windows desktop shortcut, (since mintty 2.2.3) it is suggested 
to _not_ specify an icon in the command line, as mintty detects and uses 
the icon from the invoking shortcut.
If for any reason, an icon is to be specified, it should be the same 
in the mintty command line (shortcut properties Target:) as in the 
shortcut itself (Change Icon...).

### Taskbar icon grouping ###

Windows 7 and above use the application ID for grouping taskbar items.
By default this setting is empty, in which case Windows groups taskbar
items automatically based on their icon and command line.  This can be
overridden by setting the AppID to a custom string, in which case windows
with the same AppID are grouped together.

The AppID supports placeholder parameters for a flexible grouping 
configuration (see manual).
The special value `AppID=@` causes mintty to derive an implicit AppID 
from the WSL system name, in order to achieve WSL distribution-specific 
taskbar grouping. This resolves taskbar grouping problems in some cases 
(wsltty issue #96) but causes similar problems in other cases (issue #784).

_Warning:_ Using this option in a Windows desktop shortcut may 
cause trouble with taskbar grouping behaviour. If you need to do that, 
the shortcut itself should also get attached with the same AppId.

_Note:_ Since 2.9.6, if mintty is started via a Windows shortcut 
which has its own AppID, it is reused for the new mintty window in order 
to achieve proper taskbar icon grouping. This takes precedence over an 
explicit setting of the AppID option.

_Explanation:_ Note that Windows shortcut files have their own AppID.
Hence, if an AppID is specified in the mintty settings, but not on a 
taskbar-pinned shortcut for invoking mintty, clicking the pinned 
shortcut will result in a separate taskbar item for the new mintty window, 
rather than being grouped with the shortcut.

_Hint:_ To avoid AppID inconsistence and thus ungrouped taskbar icons,
the shortcut's AppID should to be set to the same string as the mintty AppID, 
which can be done using the `winappid` utility available in 
the mintty [utils repository](https://github.com/mintty/utils).
As noted above, since mintty 2.9.6, the mintty AppID does not need to be set 
anymore in this case.

### Taskbar launch commands ###

Launch commands from the taskbar icon and its menu can be configured 
with settings `AppName`, `AppLaunchCmd` and `TaskCommands` ("jump list").
This can be made persistent with command line setting `--store-taskbar-properties`.

#### Tab sessions ####

See also the section on [Virtual Tabs and Tabbar](#virtual-tabs-and-tabbar) 
for configuration of session invocations from the context menu 
with setting `SessionCommands`.

### Taskbar pinning ###

Taskbar pinning of a mintty window can be prevented with command line setting `--nopin`.

### Taskbar bell indication ###

Bell highlighting of the taskbar icon is enabled by default and can be 
disabled with option `BellTaskbar`.

### Taskbar progress indication ###

Progress indication on the taskbar icon can be switched dynamically 
or pre-configured with options `ProgressBar` and `ProgressScan`.
To enable progress indication e.g. for the MSYS2 package manager pacman:
* `ProgressBar=1`
* `ProgressScan=2`


## Window session grouping ##

For grouping of window icons in the taskbar, Windows uses the intricate 
AppID concept as explained above. For grouping of desktop windows, as used by 
the mintty session switcher or tabbar, or external window manipulation tools, 
Windows uses the distinct but likewise intricate Class concept.
Mintty provides flexible configuration to set up either of them, see manual.


## Window icons ##

The icons (taskbar icon and title bar icon) can be changed dynamically 
with an OSC I escape sequence. Example:

```
echo -e "\e]I;`printenv 'ProgramFiles(x86)'`/Mozilla Firefox/firefox.exe,4\a"
```


## Start errors ##

### Error: could not fork child process ###

If you are frequently facing this problem, it is not really a mintty issue, 
but it may reportedly help if you turn off the Windows ASLR feature 
for cygwin-based programs; turn off Mandatory ASLR for mintty, 
cygwin-console-helper, your shell and other programs as described in 
[issue #493](https://github.com/mintty/mintty/issues/493#issuecomment-361281995)
or using Powershell commands as described in
[wsltty issue #6](https://github.com/mintty/wsltty/issues/6#issuecomment-419589599).


## Supporting Linux/Posix subsystems ##

If you have any Linux distribution for the Windows Subsystem for Linux (WSL) 
installed, mintty can be called from cygwin to run a WSL terminal session:
* `mintty --WSL=Ubuntu`
* `mintty --WSL` (for the Default distribution as set with `wslconfig /s` or `wsl -s`)

Note, the `wslbridge2` gateways need to be installed in `/bin` for this purpose 
(see below for details).

A WSL terminal session can be configured for the mintty session launcher 
in the config file, like:
* `SessionCommands=Ubuntu:--WSL=Ubuntu`

### WSLtty, the standalone WSL mintty terminal ###

For a standalone mintty deployment as a WSL terminal, also providing 
desktop and start menu shortcuts, command line launch scripts, and 
optional Windows Explorer integration, install 
[wsltty](https://github.com/mintty/wsltty),
using either the wsltty installer, a Chocolatey package, or a Windows Appx package.

### Manual setup of WSL terminal ###

To help reproduce the installation manually, for users of cygwin or msys2:
* Download from the https://github.com/Biswa96/wslbridge2 repository
* Install package dependencies `make`, `g++`, `linux-headers` in WSL
* Build the wslbridge2 gateways with
  * `make RELEASE=1` for the frontends (e.g. from cygwin)
  * `wsl make RELEASE=1` or `wsl -d` _distro_ `make RELEASE=1` for the backends
* From subdirectory `bin`, install the gateway tools `wslbridge2.exe` and `wslbridge2-backend` into your `/bin` directory
* Make a desktop shortcut (Desktop right-click – New ▸ Shortcut) with
  * Target: `X:\cygwin64\bin\mintty.exe --WSL=`_distro_` -`, with the desired WSL distro (or empty for default)
  * Icon location (Change Icon…) as appropriate; the wsltty installer will find the distro-specific icon

Replace `X:\cygwin64` with your cygwin or msys2 root directory path 
and `Linux_Distribution` with your preferred distribution. The suitable 
icon location for each respective distribution is not easily found; the 
standalone package would set that up for you in the shortcuts. For other 
invocation (cygwin or Windows command line), mintty finds the suitable 
WSL icon itself.

Note that older wslbridge2 backends used not to be interoperable among 
systems with different libraries (glibc vs musl).
The _distro_ `Alpine` had been checked out to build a backend that works 
with all Linux distributions; builds of later wslbridge2 are reported 
to be interoperable though.

At the end of the `mintty --WSL` invocation line, you may add an explicit 
WSL shell invocation like `/bin/bash -l` to select your favourite shell or 
ask for a login shell (`-l`), or set a start directory (`-C`, before any 
shell command) if desired.

If no start directory is otherwise selected, the “Start in:” directory 
of the shortcut may be set to `%USERPROFILE%`.

### Interix ###

On Windows 7, mintty may also be used as a terminal for the 
Subsystem for UNIX-based applications (SUA), also known as Interix.
For the mintty session launcher, this can be configured for the 
available shells as follows (concatenated with ‘;’ separator for multiple targets):
* `SessionCommands=Interix Korn Shell:/bin/winpty C:\Windows\posix.exe /u /c /bin/ksh -l`
* `SessionCommands=Interix SVR-5 Korn Shell:/bin/winpty posix /u /p /svr-5/bin/ksh /c -ksh`
* `SessionCommands=Interix C Shell:/bin/winpty posix /u /c /bin/csh -l`

For a desktop or start menu shortcut, the respective target command would 
look like `X:\cygwin\bin\mintty.exe winpty` …
(and may use the icon location 
%SystemRoot%\Installer\{DB88A98A-792B-4441-8E60-05A6D3E2B2C0}\sh.exe).


## Starting mintty from a batch file ##

In order to start mintty from a batch file it needs to be invoked through the **[start](http://technet.microsoft.com/en-us/library/cc770297.aspx)** command. This avoids the batch file’s console window staying open while mintty is running. For example:

```
start mintty -
```

The console window for the batch file will still show up briefly, however. This can be avoided by invoking mintty from a shortcut instead, as described above.


## Starting in a particular directory ##

The working directory for a mintty session can be set in the 
_Start In_ field of a shortcut, 
or by changing directory in an invoking script, or with option `--dir`.
Note, however, that Cygwin’s _/etc/profile_ script for login shells automatically changes to the user’s home directory.
The profile script can be told not to do this by setting a variable called _CHERE\_INVOKING_, like this:

```
mintty /bin/env CHERE_INVOKING=1 /bin/bash -l
```

Note: If mintty is run from a shortcut with empty _Start In_ field and the 
effective start directory is within the Windows system folder, mintty changes 
it in order to avoid failure when creating a log file.


## Creating a folder context menu entry for mintty ##

Cygwin’s **chere** package can be used to create folder context menu entries in Explorer, which allow a shell to be opened with the working directory set to the selected folder.

The following command will create an entry called _Bash Prompt Here_ for the current user that will invoke bash running in mintty. See the chere manual (_man chere_) for all the options.

```
chere -1 -i -c -t mintty
```

Note, however, that context menu entries created by **chere** fail on non-ASCII directory names.
Mintty option `--dir` comes to help, like in either of these registry entries 
for registry keys like `/HKCU/Software/Classes/Directory/Shell/mintty-here/command/`:
```
C:\cygwin64\bin\mintty.exe --dir "%1" /bin/bash
C:\cygwin64\bin\mintty.exe --dir "%1" /bin/env CHERE_INVOKING=1 /bin/bash -l
```


## Setting environment variables ##

Unfortunately Windows shortcuts do not allow the setting of environment variables. Variables can be set globally though via a button on the _Advanced_ tab of the system properties. Those can be reached by right-clicking on the _(My) Computer_ entry in the start menu or on the desktop, selecting _Properties_, then _Advanced System Settings_.

Alternatively, global variables can be set using the **[setx](http://technet.microsoft.com/en-us/library/cc755104.aspx)** command line utility. This comes pre-installed with some versions of Windows but is also available as part of the freely downloadable _Windows 2003 Resource Kit Tools_.

The **[env](http://www.opengroup.org/onlinepubs/9699919799/utilities/env.html)** utility can be used to set variables specifically for the program to be run in mintty, e.g.:

```
mintty /bin/env DISPLAY=:0 /bin/ssh -X server
```


## Input/Output interaction with alien programs ##

When interacting with programs that use a native Windows API for 
command-line user interaction (“console mode”), a number of undesirable 
effects used to be observed; this is the 
[pty incompatibility problem](https://github.com/mintty/mintty/issues/56) 
and the 
[character encoding incompatibility problem](https://github.com/mintty/mintty/issues/376).
This would basically affect all programs not compiled in a cygwin or msys 
environment (and note that MinGW is not msys in this context), and would 
occur in all pty-based terminals (like xterm, rxvt etc).

Cygwin 3.1.0 compensates for this issue via the ConPTY API of Windows 10.
On MSYS2, its usage can be enabled by setting the environment variable 
MSYS=enable_pcon (or selecting this setting when installing an older version).
You can also later set `MSYS=enable_pcon` in file `/etc/git-bash.config`.
MSYS2 releases since 2022-10-28 enable ConPTY by default.
You can also set mintty option `ConPTY=true` to override the MSYS2 setting.

As a workaround on older versions of Cygwin or Windows, you can use 
[winpty](https://github.com/rprichard/winpty) as a wrapper to invoke 
the Windows program.

### Signal processing with alien programs ###

The same workaround handles interrupt signals, particularly Control+C, 
which does not otherwise function as expected with non-cygwin programs.


## Terminal type detection – check if running inside mintty ##

Some applications, often text editors, want to know which terminal they 
are running in, in order to make use of terminal-specific features that 
are not indicated by the terminfo/termcap mechanism.

The most reliable way to determine the terminal type is to use the 
Secondary Device Attributes report queried from the terminal.
The script `terminal` in the mintty 
[utils repository](https://github.com/mintty/utils) provides an implementation.

In addition, from mintty 3.1.5, an additional escape sequence causes mintty 
to report its name and version; furthermore, although using 
environment variables for this purpose is not reliable (see 
[issue #776](https://github.com/mintty/mintty/issues/776) for a discussion), 
mintty sets environment variables TERM_PROGRAM and TERM_PROGRAM_VERSION 
as various other terminals do.


## Terminal line settings ##

Terminal line settings can be viewed or changed with the **[stty](http://www.opengroup.org/onlinepubs/9699919799/utilities/stty.html)** utility, which is installed as part of Cygwin’s core utilities package. Among other things, it can set the control characters used for generating signals or editing an input line.

See the stty manual for all the details, but here are a few examples. The commands can be included in shell startup files to make them permanent.

To change the key for deleting a whole word from _Ctrl+W_ to _Ctrl+Backspace_,
you could assign the `^_` control character to the _Ctrl+Backarrow_ key:
```
KeyFunctions=C+Back:"^_"
```
and apply the following terminal line setting:
```
stty werase '^_'
```

To use _Pause_ and _Break_ instead of _Ctrl+Z_ and _Ctrl+C_ for suspending or interrupting a process:

```
stty susp '^]' swtch '^]' intr '^\'
```

With these settings, the _Esc_ key can also be used to interrupt processes by setting its keycode to `^\`:

```
echo -ne '\e[?7728h'
```

(The standard escape character `^[` cannot be used for that purpose because it appears as the first character in many keycodes.)

Unix terminal line drivers have a flow control feature that allow terminal output to be stopped with _Ctrl+S_ and restarted with _Ctrl+Q_. However, due to the scrollback feature in modern terminal emulators, there is little need for this. Hence, to make those key combinations available for other uses, disable flow control with this command
(note that screen-oriented programs like text editors will typically 
manage this setting themselves):

```
stty -ixon
```


## Readline configuration ##

Keyboard input for the **[bash](http://www.gnu.org/software/bash)** shell and other programs that use the **[readline](http://tiswww.case.edu/php/chet/readline/rltop.html)** library can be configured with the so-called **[inputrc](http://tiswww.case.edu/php/chet/readline/readline.html#SEC9)** file. Unless overridden by setting the _INPUTRC_ variable, this is located at _~/.inputrc_. It consists of bindings of keycodes to readline commands, whereby comments start with a hash character. See its manual for details.

Anyone used to Windows key combinations for editing text might find the following bindings useful:

```
# Ctrl+Right / Ctrl+Left to move by whole words
"\e[1;5C": forward-word
"\e[1;5D": backward-word

# Ctrl+Delete / Ctrl+Backarrow to delete whole words
"\e[3;5~": kill-word
"\C-_": backward-kill-word      # with mintty setting KeyFunctions=C+Back:"^_"

# Ctrl+Shift+Delete to delete to end of the line
"\e[3;6~": kill-line

# Ctrl+Shift+Backarrow to delete to start of the line
"\e[72;6~": backward-kill-line  # with mintty setting KeyFunctions=CS+Back:72

# Alt-Backarrow for undo
"\e\d": undo                    # would be disabled by DECSET 67 sequence
```

Finally, a couple of bindings for convenient searching of the command history. Just enter the first few characters of a previous command and press _Ctrl+Up_ to look it up.

```
# Ctrl+Up / Ctrl+Down for searching command history
"\e[1;5A": history-search-backward
"\e[1;5B": history-search-forward
```

To add interactive protection from injected commands, the readline feature 
`enable-bracketed-paste` makes use of bracketed-paste mode to present 
multi-line commands for confirmation before running them.
It should _not_ be combined with `skip-csi-sequence` however which is 
buggy as of bash 4.4.12 / readline 7.0.3.

```
set enable-bracketed-paste on
# do not map "\e[": skip-csi-sequence
```


## Unexpected behaviour with certain applications (e.g. vim) ##

If for example the PgUp and PgDn keys do not work in your editor, the reason 
may be that in the mintty Options, the Terminal Type was set to "vt100" 
and based on the resulting setting of the environment variable TERM, 
the application expects other key sequences than mintty sends.
(While mintty could be changed to send VT100 application keypad codes in 
that case, the current behaviour is compatible with xterm.)

### Control+H in emacs ###

If you configure the Backarrow key to send a Backspace character rather 
than the Linux default DEL character (setting `BackspaceSendsBS=yes`), 
emacs will not be able to recognize an explicit Ctrl+h command anymore.
It is recommended to leave this setting at its default.

### Shift+up/down for text selection in emacs ###

The escape sequences for Shift+up/down are mapped to scroll-backward/forward 
virtual keys by the xterm terminfo entry.
Follow the advice in 
[How to fix emacs shift-up key...](https://stackoverflow.com/questions/18689655/how-to-fix-emacs-shift-up-key-for-text-selection-in-mintty-on-cygwin/#18689656)
to create a fixed terminfo entry that removes this mapping,
or use a suitable alternative setting for the environment variable TERM, 
for example `TERM=xterm-color`.

### Mode-dependent cursor in vim ###

Mintty supports control sequences for changing cursor style. These can be used to configure **[vim](http://www.vim.org/)** such that the cursor changes depending on mode. For example, with the following lines in _~/.vimrc_, vim will show a block cursor in normal mode and a line cursor in insert mode:

```
let &t_ti.="\e[1 q"
let &t_SI.="\e[5 q"
let &t_EI.="\e[1 q"
let &t_te.="\e[0 q"
```

### Enabling full mouse functionality in vim ###

Before vim 8.1.0566, full mouse mode is not automatically enabled in mintty.
Add this to _~/.vimrc_ for a workaround:

```
set mouse=a
if has("mouse_sgr")
    set ttymouse=sgr
else
    set ttymouse=xterm2
end
```

### Blinking cursor reset ###

Some applications may reset cursor style, especially cursor blinking, 
after terminating, caused by the 
[terminfo database](http://invisible-island.net/ncurses/man/terminfo.5.html) 
including the corresponding reset sequence in the “normal cursor” setting.
This is avoided with mintty option `SuppressDEC=12`, not needed from mintty 3.0.1.

### Avoiding escape timeout issues in vim ###

It’s a historical flaw of Unix terminals that the keycode of the escape key, i.e. the escape character, also appears at the start of many other keycodes. This means that on seeing an escape character, an application cannot be sure whether to treat it as an escape key press or whether to expect more characters to complete a longer keycode.

Therefore they tend to employ a timeout to decide. The delay on the escape key can be annoying though, particularly with the mode-dependent cursor above enabled.  The timeout approach can also fail on slow connections or a heavily loaded machine.

Mintty’s “application escape key mode” can be used to avoid this by switching the escape key to an unambiguous keycode. Add the following to _~/.vimrc_ to employ it in vim:

```
let &t_ti.="\e[?7727h"
let &t_te.="\e[?7727l"
noremap <Esc>O[ <Esc>
noremap! <Esc>O[ <Esc>
```

Note that the last line causes vi-compatible behaviour to be used when pressing Esc on the command line: the command is executed rather than cancelled as is the default for vim. If the latter is preferred, replace the last line with a mapping to `^C`:

```
noremap! <Esc>O[ <C-c>
```


## Keyboard issues in specific environments ##

### Detecting AltGr in TeamViewer ###

Windows provides AltGr using two virtual key codes (Ctrl and Menu) 
sharing the same timestamp. TeamViewer is buggy with respect to the 
timestamp. As a workaround, mintty can detect AltGr also from the 
two key codes arriving with some delay. Setting 
`CtrlAltDelayAltGr=16` or `CtrlAltDelayAltGr=20` is suggested.

### Handling the clipboard in Hot Keyboard ###

Hot Keyboard needs configuration `CtrlAltIsAltGr=1` and `SupportExternalHotkeys=4` 
as a workaround for buggy behaviour. Also `ClipShortcuts=true` (default) is advisable.


## Using Ctrl+Tab to switch window pane in terminal multiplexers ##

The _Ctrl+Tab_ and _Ctrl+Shift+Tab_ key combinations can be used to 
switch windows/panes/tabs in a terminal multiplexer session.
In order to do so, their use as shortcuts for switching mintty windows 
needs to be disabled on the _Keys_ page of the options, 
and their keycodes need to be mapped as shown below.

### Switch window in GNU Screen ###

For **[GNU Screen](http://www.gnu.org/software/screen)**, in _~/.screenrc_:

```
bindkey "^[[1;5I" next
bindkey "^[[1;6I" prev
```

### Switch pane in tmux ###

For **[tmux](https://tmux.github.io/)**, in _~/.tmux.conf_:

```
set -s user-keys[0] "\e[1;5I"
set -s user-keys[1] "\e[1;6I"
bind-key -n User0 next-window
bind-key -n User1 previous-window
```


## Keyboard customization ##

A number of options are available to customize the keyboard behaviour, 
including user-defined function and keypad keys and Ctrl+Shift+key shortcuts.
See the manual page for options and details about
* Backspace/DEL behaviour
* AltGr and other modifiers
* shortcut assignments
* special key assignments
* user-definable key functions

See also the [[Keycodes]] wiki page.

### Backarrow key configuration ###

By default, mintty sends `^?` (ASCII DEL) as the keycode for the Backarrow key.
This is the Linux default (as opposed to sending `^H` which was the 
default in many Unix environments).
This can be changed with setting `BackspaceSendsBS=yes`.
The tty setting `ERASE character` will be aligned accordingly 
(see `man termios` and command `stty erase`).

Mind that this may affect certain applications, for example emacs which 
cannot interpret the explicit Control+h command anymore.

### Windows-style copy/paste key assignments ###

If both settings `CtrlShiftShortcuts` and `CtrlExchangeShift` are enabled, 
copy & paste functions are assigned to plain (unshifted) _Ctrl+C_ 
and _Ctrl+V_ for those who prefer them to be handled like in Windows.

It’s also possible to assign user-defined functions to modified 
character keys with setting `KeyFunctions`; however, redefining 
control character assignment (e.g. Control+C) or Alt-modified characters 
(prefixing them with ESC) is not supported by default. 
This would disable basic terminal features and may result in users 
„shooting themselves in the foot“; overriding this protection is 
possible by setting `ShootFoot`.

Finally, it’s also possible to define Control+V as a paste function 
independently of the terminal; add the following to your .bashrc file:
```
paste () {
  CLIP=$(cat /dev/clipboard)
  COUNT=$(echo -n "$CLIP" | wc -c)
  READLINE_LINE="${READLINE_LINE:0:$READLINE_POINT}${CLIP}${READLINE_LINE:$READLINE_POINT}"
  READLINE_POINT=$(($READLINE_POINT + $COUNT))
}
bind -x '"\C-v": paste'
```
and the following to your .inputrc file:
```
set bind-tty-special-chars off
```


## Compose key ##

Mintty uses the Windows keyboard layout system with its “dead key” mechanism 
for entering accented characters, enhanced by self-composed characters 
for dead-key combinations that Windows does not support (e.g. ẃ).

Mintty also provides a Compose key, configurable to Control, Shift or Alt,
using X11 compose data. For example, if the compose key is configured 
to be Control, pressing and release the Control key, followed by letters 
`a` and `e`, will enter `æ`; Control-`-`-`,` will enter `¬`, 
Control-`C`-`o` will enter `©`, Control-`<`-`<` will enter `«`, 
Control-`c`-`,` will enter `ç`, Control-`s`-`s` will enter `ß`, 
Control-`!`-`!` will enter `¡`, Control-`!`-`?` will enter `‽`, etc.

For a separate compose key solution, the most seamless and stable 
**Compose Key for Windows** is 
**[WinCompose](https://github.com/SamHocevar/wincompose)**.


## Appearance ##

### Changing colours ###

The default foreground, background and cursor colours can be changed in the options dialog, or by specifying the _ForegroundColour_, _BackgroundColour_ and _CursorColour_ settings in the configuration file or on the command line.

However, they can also be changed from within the terminal using the xterm control sequences for this purpose, for example:

```
echo -ne '\e]10;#000000\a'  # Black foreground
echo -ne '\e]11;#C0C0C0\a'  # Light gray background
echo -ne '\e]12;#00FF00\a'  # Green cursor
```

In mintty, the RGB colour values can also be specified using a comma-separated decimal notation, 
for example `255,0,0` instead of `#FF0000` for red. 
[X11 colour names](http://en.wikipedia.org/wiki/X11_color_names) 
are supported, too. See the examples below for all options.

The 16 [ANSI colours](http://en.wikipedia.org/wiki/ANSI_escape_code#Colors) 
can be set in the configuration file or on the command line using settings 
such as _Blue_ or _BoldMagenta_. These are documented in the 
[configuration section](http://mintty.github.io/mintty.1.html#CONFIGURATION) 
of the manual. They can also be changed using xterm control sequences. 
Here they are with their default values:

```
echo -ne '\e]4;0;#000000\a'   # black
echo -ne '\e]4;1;#BF0000\a'   # red
echo -ne '\e]4;2;#00BF00\a'   # green
echo -ne '\e]4;3;#BFBF00\a'   # yellow
echo -ne '\e]4;4;#0000BF\a'   # blue
echo -ne '\e]4;5;#BF00BF\a'   # magenta
echo -ne '\e]4;6;#00BFBF\a'   # cyan
echo -ne '\e]4;7;#BFBFBF\a'   # white (light grey really)
echo -ne '\e]4;8;#404040\a'   # bold black (i.e. dark grey)
echo -ne '\e]4;9;#FF4040\a'   # bold red
echo -ne '\e]4;10;#40FF40\a'  # bold green
echo -ne '\e]4;11;#FFFF40\a'  # bold yellow
echo -ne '\e]4;12;#6060FF\a'  # bold blue
echo -ne '\e]4;13;#FF40FF\a'  # bold magenta
echo -ne '\e]4;14;#40FFFF\a'  # bold cyan
echo -ne '\e]4;15;#FFFFFF\a'  # bold white
```

Different notations are accepted for colour specifications:
* ```#RRGGBB``` (256 hex values, see examples above)
* ```rrr,ggg,bbb``` (256 decimal values)
* ```rgb:RR/GG/BB``` (256 hex values)
* ```rgb:RRRR/GGGG/BBBB``` (65536 hex values)
* ```cmy:C.C/M.M/Y.Y``` (float values between 0 and 1)
* ```cmyk:C.C/M.M/Y.Y/K.K``` (float values between 0 and 1)
* _color-name_ (using X11 color names, e.g. ```echo -ne '\e]10;bisque2\a'```)

### Using colour schemes (“Themes”) ###

Colour schemes (that redefine ANSI colours and possibly foreground/background 
colours) can be loaded with the option ```-C``` (capital C) or ```--loadconfig``` 
which loads a configuration file read-only, i.e. configuration changes 
are not saved to this file, or with the new setting _ThemeFile_.

In the Options menu, section _Looks_, the _Theme_ popup offers theme files 
as stored in a resource directory for selection.
This dialog field (or the “Color Scheme Designer” button for drag-and-drop) 
can be used in different ways:
* (*) Popup the selection to choose a theme configured in your resource directory
* Insert a file name (e.g. by pasting or drag-and-drop from Windows Explorer)
* (*) Drag-and-drop a theme file from the Internet (may be embedded in HTML page)
* (*) Drag-and-drop a colour scheme directly from the Color Scheme Designer (see below)

(* Option 1) 
The default theme (since 3.6.0) is 
[helmholtz](https://raw.githubusercontent.com/mintty/mintty/master/themes/helmholtz) 
which provides a colour scheme of well-balanced appearance and luminance;
see the comments in the theme file about its crafting principles.

(* Option 3) 
A number of colour schemes have been published for mintty, also 
mintty supports direct drag-and-drop import of theme files in 
iTerm2 or Windows terminal formats.
Look for the following repositories:
* https://iterm2colorschemes.com/
* https://github.com/oumu/mintty-color-schemes
* https://github.com/goreliu/wsl-terminal/tree/master/src/etc/themes

After drag-and-drop of a colour scheme, it is automatically applied 
to the current terminal session for quick and easy testing;
to keep the scheme in your popup selection, assign a name to it by typing it 
into the Theme field, then click the “Store” button. After downloading a 
theme file, the name will be filled with its basename as a suggestion.
As long as a colour scheme is loaded but not yet stored, and a name is 
available in the Theme field, the “Store” button will be enabled.

(* Option 4) The 
[4bit Terminal Color Scheme Designer](http://ciembor.github.io/4bit/#) 
lets you download a tuned colour scheme (top-right button “Get Scheme”).
Click on the button “Color Scheme Designer” below the Theme field 
to open the designer page and start your design. You can either download 
the scheme file (“Get Scheme” – “mintty”) or drag-and-drop the download link 
directly to the mintty Options menu, to either the Theme field or the 
Color Scheme Designer button. If you like the scheme, you can enter a 
theme name in the Theme field and then click the “Store” button to 
store the colour scheme.

Mintty also provides the command-line script ```mintheme``` which can 
display the themes available in the mintty configuration directories or 
activate one of them in the current mintty window.

### Background image ###

As an alternative to a background colour, mintty also supports graphic 
background. This can be configured with the option `Background` or 
set dynamically using special syntax of the colour background OSC sequence.
The respective parameter addresses an image file, preceded by a mode 
prefix and optionally followed by a transparency value.
Prefixes are:
* `*` use image file as tiled background
* `_` (optional with option Background) use image as picture background, scaled to window
* `%` use image as picture background and scale window to its aspect ratio
* `=` use desktop background (if tiled and unscaled), for a virtual floating window

If the background filename is followed by a comma and a number between 1 and 254, 
the background image will be dimmed towards the background colour;
with a value of 255, the alpha transparency values of the image will be used.

Examples:
```
Background=C:\cygwin64\usr\share\backgrounds\tiles\rough_paper.png
-o Background='C:\cygwin64\usr\share\backgrounds\tiles\rough_paper.png'
echo -ne '\e]11;*/usr/share/backgrounds/tiles/rough_paper.png\a'
echo -ne '\e]11;_pontneuf.png,99\a'
echo -ne '\e]11;=,99\a'
```

Note that relative pathnames depend on proper detection of the current directory 
of the foreground process.
Note that absolute pathnames within the cygwin file system are likely 
not to work among different cygwin installations. 
To configure a background in `$APPDATA/mintty/config` (or 
`%APPDATA%/wsltty/config`), Windows pathname syntax should be used.


## Providing and selecting fonts ##

To provide additional fonts for use with mintty, monospace fonts can be 
installed in Windows. Note that font installation in X11 does not make 
a font available for mintty as mintty is not an X windows application.
Some monospace fonts are not explicitly marked as such in the font file.
In that case the font will not be listed in the mintty 
Options – Text – Font selection menu. 
It can still be used by explicit selection, e.g.:

```
mintty -o Font="Linux Libertine Mono"
```

Also, Unicode font names are now supported, e.g.
```
mintty -o Font=Sütterlin
mintty -o Font=옹달샘
```

The font selection menu lists monospace fonts unless marked to Hide 
in the Fonts folder of the system Control Panel.
To include them in the fonts offered in the menu (e.g. to select any of 
DotumChe, GulimChe, GungsuhChe, MingLiU, MS Gothic, MS Mincho, NSimSun, 
Simplified Arabic Fixed), do either of:
* Uncheck “Hide fonts based on language settings” in Fonts ▸ Font settings
* Hide/Show fonts individually from their context menu
* Set the mintty hidden setting ShowHiddenFonts=true

The latter setting also includes fonts with an OEM or SYMBOL character set.

Some fonts with a name problem (e.g. Meslo LG S for Powerline) can be 
selected using the new Apply button in the font selection menu.

Fonts not listed in the menu can be configured with the Font setting.

The old font selection and menu format can be chosen with setting `FontMenu=1`.

If you are missing certain characters, e.g. as used for the popular “Powerline” plugin,
the reason may be that specifically designed characters are being addressed 
that are provided in the Unicode Private Use range of dedicated fonts;
a collection of such fonts can be found at [Nerd Fonts](http://nerdfonts.com/).

### Alternative fonts ###

Mintty supports up to 10 alternative fonts that can be selected as 
character attributes (see Text attributes below). They are configured 
in the config file (see manual page), except for font 10 which has a 
default preference; mintty will try to find a Fraktur or Blackletter font 
for it on your system.
<img align=top src=https://github.com/mintty/mintty/wiki/mintty-alternative-fonts.png>

### Secondary fonts ###

Mintty can select alternative fonts for specific Unicode script ranges.
With this feature, you can e.g. use a different font for CJK characters.

Script names are as specified in the Unicode file Scripts.txt, listed in 
[wiki: Unicode scripts](https://en.wikipedia.org/wiki/Script_(Unicode)) column "Alias".

A special name is `CJK` which comprises Han, Hangul, Katakana, Hiragana, Bopomofo, Kanbun, 
Halfwidth and Fullwidth Forms (except Latin). A later more specific entry 
will override an earlier one (see CJK example below).

Configuration example:
```
FontChoice=Hebrew:6;Arabic:7;CJK:5;Han:8;Hangul:9
Font6=David
Font7=Simplified Arabic Fixed
Font5=Malgun Gothic
Font8=FangSong
Font9=MingLiU
```

Another special name is `PictoSymbols` to assign an alternative font to 
ranges of pictographic symbols, including arrows, mathematical and technical 
symbols, shapes, dingbats, emoticons etc.
Configuration example:
```
FontChoice=PictoSymbols:2
Font2=DejaVu Sans Mono
```

Finally, special name `Private` covers the Private Use ranges, which are 
often used for additional icon symbols (e.g. by "Nerd Fonts" or "Powerline" fonts).
Configuration example:
```
FontChoice=Private:3
Font3=MesloLGS NF
```

Note that in addition to Unicode scripts, also Unicode blocks can be used 
to specify a secondary font, by a "|" prefix to the block name, with block 
specifications preceding over the more general script specifications.
```
FontChoice=Greek:3;|Greek Extended:4
```

### Dynamic fonts ###

Mintty supports on-the-fly temporary font installation, especially for use 
in a portable terminal application.
Place font files into the subdirectory `fonts` of the mintty configuration 
directory. All will be made available for mintty usage, there is currently 
no mechanism to configure dynamic fonts explicitly. The number of font files 
should be limited to avoid significant startup delay.
To avoid having to set up an explicit `--configdir` invocation parameter, 
fonts can be placed in the /usr/share/mintty/fonts folder of the portable 
installation.


## Character width ##

By default, mintty adjusts character width to the width assumption of the 
locale mechanism (function `wcwidth`).
Character width can be modified by a number of configuration or dynamic settings:
* `Locale`: change locale, stays consistent with system locales
* `Charwidth=unicode`: use built-in rather than system-provided Unicode data
* `Charwidth`: further options to handle double-width characters
* `Charset`: may affect CJK ambiguous-width handling if used with `Locale`
* `Font`: may affect CJK ambiguous-width handling if locale support fails
* `PrintableControls`: makes C1 or C0 control characters visible (width 1)
* [OSC 701](https://github.com/mintty/mintty/wiki/CtrlSeqs#locale): changes locale/charset, may affect ambiguous width handling
* OSC 50: changes font, may affect ambiguous width handling (with `Locale`)
* [OSC 77119](https://github.com/mintty/mintty/wiki/CtrlSeqs#wide-characters): turns some character ranges to wide characters
* [PEC](https://github.com/mintty/mintty/wiki/CtrlSeqs#explicit-character-width): explicit character width attribute

See the [mintty manual](http://mintty.github.io/mintty.1.html) and
[Control Sequences](https://github.com/mintty/mintty/wiki/CtrlSeqs)
for more details.

Note that with any of these settings, actual width properties as 
rendered on the screen and width assumptions of the `wcwidth` function 
will be inconsistent then for the impacted characters, which may confuse 
screen applications (such as editors) that rely on `wcwidth` information.

### Ambiguous width setting ###

A number of Unicode characters have an “ambiguous width” property due to 
legacy issues with dedicated CJK fonts, meaning they can be narrow 
(single-cell width) or wide (double-cell width) in a terminal.

To select ambiguous-width characters to appear wide (as some applications 
may expect), mintty should be run in a CJK locale (character encoding does 
not need to be CJK), e.g.:

```
LC_CTYPE=zh_SG.utf8 mintty &
```

If the locale is selected via the Locale setting, however, it is necessary 
to choose an ambiguous-wide font in addition (CJK font), or mintty will 
enforce the ambiguous-narrow mode of rendering by appending the 
“@cjknarrow” locale modifier:

```
mintty -o Locale=zh_CN -o Font=FangSong &
```

If it is not desired to set a specific base locale in order to enable 
ambiguous-wide mode, option `Charwidth=ambig-wide` can be used.
It implies `Charwidth=unicode` behaviour, with the same caveats as above.
Mintty indicates this mode by appending the `@cjkwide` modifier to the 
`LC_CTYPE` locale variable.

Single-width CJK character rendering can be enforced, with proper glyph 
scaling, with settings `Charwidth=single` or `Charwidth=single-unicode`, 
following a 
[proposal](https://gitlab.freedesktop.org/terminal-wg/specifications/issues/9#note_406682) 
in the Terminals Working Group Specifications.
Mintty indicates this mode by appending the `@cjksingle` modifier to the 
`LC_CTYPE` locale variable.

### Remote locale width mismatch ###

After remote login, locale definitions of remote and local systems may differ, 
mostly about existing locales and ambiguous width properties.
This is particularly the case if
* the cygwin locale includes a @cjk... modifier which is not supported on other systems
* the cygwin locale is a CJK locale with UTF-8 encoding in cygwin before 3.2.0 (which makes ambiguous width narrow for all UTF-8 locales to be consistent with other systems)

The script `localejoin` in the 
mintty [utils repository](https://github.com/mintty/utils) adjusts these 
mismatches by switching the terminal locale temporarily and thus joining 
its width properties with those of typical remote systems.
Direct invocation of the script just runs a shell with a locale setting.
To use it for direct remote invocation, install it under the name 
containing any of the remote login programs (rsh, rlogin, rexec, telnet, ssh).
Example (with `localejoin` renamed/linked/copied as `minssh`):
* `minssh` _myLinuxhost_

### Selective double character width ###

While mintty fully supports double-width characters (esp. CJK) as well 
as ambiguous-width characters, there are also characters of fuzzy 
width property, because their rendered glyph is wider than one 
terminal character cell in most fonts, but yet they are defined as 
single-width by Unicode. Such characters often appear to be clipped 
on the screen. Mintty has an experimental feature to display semi-wide 
Indic and some other characters at double-cell width
(see [Control Sequences – Wide characters](https://github.com/mintty/mintty/wiki/CtrlSeqs#wide-characters)),
but not all such characters are handled, and there is no perfect solution 
that would also comply with the locale mechanism unless the terminal would 
support proportional fonts.

### Wide symbol overhang ###

For symbol characters and emojis that are single-width by definition 
(e.g. locale) but visually double-width, double-width display is supported 
if the character is followed by an adjacent single-width space character.


## Font rendering and geometry ##

Mintty can make use of advanced Windows font fallback as provided via the Uniscribe API, 
achieving improved character/glyph substitution for characters not provided in the selected font.
Option `-o FontRender=uniscribe` is now the default, `-o FontRender=textout` disables it.
Note that Uniscribe is not applied to right-to-left text as it would 
interfere with mintty’s own bidi transformation.

### Window geometry, rows and columns ###

The actual window size is influenced by several parameters:
* Font size / character height is the main parameter to determine the row height.
* Row height is additionally affected by the “leading” information from the font.
* Automatic row height adjustment method can be selected by setting `AutoLeading`.
* Row height and column width can furthermore be tuned with setting `RowSpacing` and `ColSpacing`.
* A gap between text and window border can be specified with setting `Padding` (default 1).

Note: The term “leading” (pronounced like “ledding”) comes from the 
times of metal typesetting when strips of lead (the metal) were used 
to adjust line spacing.


## Text attributes and rendering ##

Mintty supports a maximum of usual and unusual text attributes, 
settable with “Select Graphic Rendition” (SGR) escape sequences.
For underline styles and some other values, colon-separated 
ECMA-48 sub-parameters are supported.

| **start `^[[...m`**    | **end `^[[...m`** | **attribute**                 |
|:-----------------------|:------------------|:------------------------------|
| 1                      | 22                | bold                          |
| 2                      | 22                | dim                           |
| 1:2                    | 22                | shadowed                      |
| 3                      | 23                | italic                        |
| 4 _or_ 4:1             | 24 _or_ 4:0       | solid underline               |
| 4:2 _or_ 21            | 24 _or_ 4:0       | double underline              |
| 4:3                    | 24 _or_ 4:0       | wavy underline                |
| 4:4                    | 24 _or_ 4:0       | dotted underline              |
| 4:5                    | 24 _or_ 4:0       | dashed underline              |
| 5                      | 25                | blinking                      |
| 6                      | 25                | rapidly blinking              |
| 7                      | 27                | inverse                       |
| 8                      | 28                | invisible                     |
| 8:7                    | 28                | overstrike                    |
| 9                      | 29                | strikeout                     |
| 11 (*)                 | 10                | alternative font 1 (*)        |
| 12                     | 10                | alternative font 2            |
| ...                    | 10                | alternative fonts 3...8       |
| 19                     | 10                | alternative font 9            |
| 20                     | 23 _or_ 10        | Fraktur/Blackletter font      |
| 21 _or_ 4:2            | 24 _or_ 4:0       | double underline              |
| 53                     | 55                | overline                      |
| 30...37                | 39                | foreground ANSI colour        |
| 90...97                | 39                | foreground bright ANSI colour |
| 40...47                | 49                | background ANSI colour        |
| 100...107              | 49                | background bright ANSI colour |
| 38;5;P _or_ 38:5:P     | 39                | foreground palette colour     |
| 48;5;P _or_ 48:5:P     | 49                | background palette colour     |
| 38;2;R;G;B             | 39                | foreground true colour        |
| 48;2;R;G;B             | 49                | background true colour        |
| 38:2::R:G:B            | 39                | foreground RGB true colour    |
| 48:2::R:G:B            | 49                | background RGB true colour    |
| 38:3:F:C:M:Y           | 39                | foreground CMY colour (*)     |
| 48:3:F:C:M:Y           | 49                | background CMY colour (*)     |
| 38:4:F:C:M:Y:K         | 39                | foreground CMYK colour (*)    |
| 48:4:F:C:M:Y:K         | 49                | background CMYK colour (*)    |
| 51 _or_ 52             | 54                | emoji style (*)               |
| 58:5:P                 | 59                | underline palette colour      |
| 58:2::R:G:B            | 59                | underline RGB colour          |
| 58:3:F:C:M:Y           | 59                | underline CMY colour (*)      |
| 58:4:F:C:M:Y:K         | 59                | underline CMYK colour (*)     |
| 73 _or_ ?4             | 75 _or_ ?24       | superscript (*)               |
| 74 _or_ ?5             | 75 _or_ ?24       | subscript (*)                 |
| _any_                  | 0 _or empty_      |                               |

Note: Alternative fonts are configured with options Font1 ... Font10.
They can also be dynamically changed with OSC sequence 50 which refers 
to the respectively selected font attribute.

Note: The control sequence for alternative font 1 overrides the identical 
control sequence to select the VGA character set. Configuring alternative 
font 1 is therefore discouraged. Note, on the other hand, that the 
VGA character set control sequence SGR 11 (effective if Font1 is not configured) 
is _not_ reset with SGR 0 but only with SGR 10.

Note: The control sequences for Fraktur (“Gothic”) font are described 
in ECMA-48, see also [wiki:ANSI code](https://en.wikipedia.org/wiki/ANSI_escape_code).
To use this feature, it is suggested to install `F25 Blackletter Typewriter`,
e.g. from:
* https://www.dafont.com/f25-blacklettertypewriter.font
* https://fontmeme.com/fonts/f25-blackletter-typewriter-font/

Note: RGB colour values are scaled to a maximum of 255 (=100%).
CMY(K) colour values are scaled to a maximum of the given parameter F (=100%).

Note: The emoji style attribute sets the display preference for a number 
of characters that have emojis but would be displayed with text style 
by default (e.g. decimal digits).

Note: Text attributes can be disabled with option SuppressSGR (see manual).

Note: Combined SGR 73;74 results in small characters at normal position.
This does not apply to the alternative DEC private SGRs ?4 and ?5.

As a fancy add-on feature for text attributes, mintty supports distinct 
(colour) attributes for combining characters, so a combined character 
can be displayed in multiple colours. Attributes considered for this 
purpose are default and ANSI foreground colours, palette and true-colour 
foreground colours, dim mode and manual bold mode (BoldAsFont=false), 
and blinking; background colours and inverse mode are ignored.
<img align=top src=https://github.com/mintty/mintty/wiki/mintty-coloured-combinings.png>


## Emojis ##

Mintty supports display of emojis as defined by Unicode using 
emoji presentation, emoji style variation and emoji sequences.
(Note that the tty must be in a UTF-8 locale to support emoji codes.)
Extended flag emojis (not listed by Unicode) are supported dynamically.

<img align=right src=https://github.com/mintty/mintty/wiki/mintty-emojis.png>

The option `Emojis` can choose among sets of emoji graphics if 
deployed in a mintty configuration directory.
With this option, mintty emoji support is enabled and the emoji graphics style is chosen. 
Mintty will match output for valid emoji sequences, 
emoji style selectors and emoji presentation forms.

For characters with default text style but optional emoji graphics,
emoji style can be selected with the “framed” or “encircled” text attribute.

Note that up to cygwin 2.10.0, it may be useful to set `Charwidth=unicode` in addition.

Emojis are displayed in the rectangular character cell group determined 
by the cumulated width of the emoji sequence characters. The option 
`EmojiPlacement` can adjust the location of emoji graphics within that area.
You can use the escape sequence PEC to tune emoji width.

### Installing emoji resources ###

Mintty does not bundle actual emoji graphics with its package.
You will have to download and deploy them yourself.
Expert options are described here, see also the next section 
for a Quick Guide to emoji installation.

Emoji data can be found at the following sources:
* [Unicode.org](http://www.unicode.org/emoji/charts/) Full Emoji List (~50MB)
  * Use the script [`getemojis`](getemojis) to download the web pages 
  [Full Emoji List](http://www.unicode.org/emoji/charts/full-emoji-list.html) and 
  [Full Emoji Modifier Sequences](http://www.unicode.org/emoji/charts/full-emoji-modifiers.html) 
  (with all emoji data embedded)
  and extract emoji data (call it without parameters for instructions)
  * Deploy the desired subdirectories (e.g. `apple`) and subdirectory `common`
  * Includes apple, emojione, facebook, google, twitter, samsung, windows emojis (and some limited low-resolution sets that we shall ignore)
* [OpenMoji](https://openmoji.org/)
  * Under “Get OpenMojis”, download the “[PNG Color 72×72](https://github.com/hfg-gmuend/openmoji/releases/latest/download/openmoji-72x72-color.zip)” archive (or the very large resolution if preferred)
  * Unpack the archive into `openmoji`
* [Noto Emoji font](https://github.com/googlefonts/noto-emoji), subdirectory `png/128`
  * “Clone or download” the repository or download a release archive
  * Deploy subdirectory noto-emoji/png/128 as `noto`
* [JoyPixels](https://www.joypixels.com/) (formerly EmojiOne)
  * Download JoyPixels Free (or Premium)
  * Deploy the preferred subdirectory (e.g. png/unicode/128) as `joypixels`
* Zoom (with an installed Zoom meeting client)
  * Deploy $APPDATA/Zoom/data/Emojis/*.png into `zoom`

Emoji flags graphics (extending Unicode) can be found at the following sources:
* [Google region flags](https://github.com/google/region-flags.git)
* [Puellanivis’ emoji flags](https://github.com/puellanivis/emoji.git)

  * Use the script [`getflags`](getflags) to download the repositories 
  and extract emoji data (call it without parameters for instructions).
  * Deploy common/*.png into `common`

To “Clone” with limited download volume, use the command `git clone --depth 1`.
To download only the desired subdirectory from `github.com`, use `subversion`, 
for example:
  * `svn export https://github.com/googlefonts/noto-emoji/trunk/png/128 noto`
  * `svn export https://github.com/iamcal/emoji-data/trunk/img-apple-160 apple`

“Deploy” above means move, link, copy or hard-link the respective subdirectory 
into mintty configuration resource subdirectory `emojis`, e.g.
* `mv noto-emoji/png/128 ~/.config/mintty/emojis/noto`
* `ln -s "$PWD"/noto-emoji/png/128 ~/.config/mintty/emojis/noto`
* `cp -rl noto-emoji/png/128 ~/.config/mintty/emojis/noto`

Use your preferred configuration directory, e.g.
* `cp -rl noto-emoji/png/128 "$APPDATA"/mintty/emojis/noto`
* `cp -rl noto-emoji/png/128 /usr/share/mintty/emojis/noto`

### Quick Guide to emoji installation ###

In the cygwin or MSYS2 mintty packages, the emoji download and deployment 
scripts are installed in /usr/share/mintty/emojis, so do this for a common 
all-users deployment of the emojis listed at Unicode.org and the flags emojis:
* `cd /usr/share/mintty/emojis`
* `./getemojis -d`
* `./getflags -de`

You may also use the scripts for deployment in your preferred config directory.

To deploy in your personal local resource folder:
* `mkdir -p ~/.config//mintty/emojis; cd ~/.config/mintty/emojis`
* `/usr/share/mintty/emojis/getemojis -d`
* `/usr/share/mintty/emojis/getflags -de`

To deploy in your personal common resource folder (shared e.g. by cygwin/MSYS2):
* `mkdir -p "$APPDATA"/mintty/emojis; cd "$APPDATA"/mintty/emojis`
* `/usr/share/mintty/emojis/getemojis -d`
* `/usr/share/mintty/emojis/getflags -de`


## Searching in the text and scrollback buffer ##

With the `Search` menu command or Alt+F3, a search bar is opened.
Matching is case-insensitive and ignores combining characters.
Matches are highlighted in the scrollback buffer.
The appearance of the search bar and the matching highlight colours can be 
customized.

Another search feature (Shift+cursor-left/right) skips to the 
previous/next prompt line if these are marked with scroll marker escape 
sequences, see the [[CtrlSeqs]] wiki page.


## Character encoding ##

Character encoding (or character set) is normally determined via the 
locale mechanism. To run mintty in a specific locale case-by-case, 
you would set the LC_CTYPE locale category (using environment variables 
LC_ALL, LC_CTYPE, LANG in this precedence) to configure both mintty and 
its child process (shell) consistently, for example:

```
LC_CTYPE=zh_CN.gbk mintty &
```

However, as a legacy option, it is also possible to configure mintty with 
distinct options `Locale` and `Charset`; note that despite the name, the 
`Locale` parameter must *not* include an encoding suffix in this case.
Note that combining `Locale` with an empty `Charset` setting results in 
the implicit character encoding as defined by the respective locale 
without suffix, which is not UTF-8 in most cases and may be unexpected.
Take care to make sure that the child process has the same idea about the 
character encoding as the terminal in this scenario.

### GB18030 support ###

Mintty has special support for the GB18030 character encoding which is not 
supported by cygwin and therefore not available for interactive configuration 
of the `Charset` setting in the Options dialog.
Setting `Charset=GB18030` in a config file or on the command line invokes 
this support (setting `Locale` too is necessary).
Mintty will fallback to the GBK character encoding for the locale 
setup of its child process in this case, to provide at least 
consistence with a maximum subset of GB18030. GB18030 is fully 
supported for terminal output/input. Example:

```
mintty -o Locale=zh_CN -o Charset=GB18030 &
```
Add setting `-o Charwidth=ambig-wide` if desired.

If mintty is used as a WSL terminal, the WSL side can be configured to run 
a GB18030 locale as well to achieve full GB18030 support.

```
mintty --WSL[=...] -o Locale=zh_CN -o Charset=GB18030
```

### Passing arguments from an environment with different character set ###

To pass non-ASCII parameters to a command run from mintty using a specific 
character encoding, proper conversion must be crafted.
See [issue #463](https://github.com/mintty/mintty/issues/463) 
for a discussion.
For example, for a desktop shortcut to start a GBK-encoded mintty 
starting in a specific directory with a non-ASCII name, 
use this command line as a shortcut target:

```
C:\cygwin64\bin\mintty.exe -o Locale=C -o Charset=GBK /bin/bash -l -c "cd `echo D:/桌面 | iconv -f UTF-8`; exec bash"
```

So the initial shell, interpreting its ```cd``` parameters already in GBK 
encoding, will see it properly converted.


## Spawning a new terminal window in the same directory ##

With Alt+F2, normally another mintty window would be opened in the 
home directory (or where the current window was started), while it may 
be desirable to open it in the same directory as the current working 
directory. This can be achieved with some interaction between the shell 
and the terminal, as applied e.g. by the Mac Terminal.
The shell can inform the terminal about a changed directory with the 
OSC 7 control sequence (see the [[CtrlSeqs]] wiki page), to be output 
with the prompt (example for bash):

```
PROMPT_COMMAND='echo -ne "\e]7;$PWD\a" ; '"$PROMPT_COMMAND"
```

The sequence could also be output by shell aliases or functions changing the directory.
It cannot be embedded in the prompt itself with ```\w``` as that is using some shortcuts, 
but ```$PWD``` could be used if ```shopt promptvars``` is not unset.

Note that after remote login, the directory path may be meaningless 
unless the remote and local paths match.
Note also that from a login terminal (e.g. using parameter `-` to start 
a login shell), Alt+F2 starts again a login terminal, whose login shell 
is likely to reset the working directory to the home directory.


## Virtual Tabs and Tabbar ##

The Virtual Tabs feature provides a list of all running mintty sessions 
as well as configurable launch parameters for new sessions.
The session list is shown when right-clicking the title bar (if 
virtual tabs mode is configured or with Ctrl) or ctrl+left-clicking it.
By default, the list is also shown in the extended context menu (Ctrl+right-click), 
the mouse button 5 menu, and the menus opened with the Ctrl+Menu key 
and the Ctrl+Shift+I shortcut (if enabled).
(Menu contents for the various context menu invocations is configurable.)
For configuration, see settings `SessionCommands`, `Menu*`, 
and `SessionGeomSync`.
Distinct sets of sessions can be set up with the setting `-o Class=...`.
For flexible window grouping, this setting supports the same placeholders 
as the `AppID` option.

### Tabbar ###

Tabs can also be switched from a tabbar, activated with setting `Tabbar` to 
a recommended value of 2 or more (use 4 or 9 for maximal tab synchronization).


## Multi-monitor support ##

Mintty supports multiple monitors with two features:

A mintty window can be placed on a specific monitor with the 
command-line option `-p @N` where N is the number of the monitor.

A new mintty window cloned with Alt+F2 can be placed on a selected monitor 
while F2 is being held; press cursor and other keys on the numeric keypad 
to navigate the monitor grid to the desired target monitor, then release F2.

Note also the generic Windows hotkeys to move the current window to 
the left or right neighbour monitor: Win+Shift+cursor-left/right.


## Embedding graphics in terminal output ##

Mintty supports both Sixel graphics and image graphics output (see below).

The Sixel feature facilitates a range of applications that integrate 
graphic images in the terminal, animated graphics, and even video and 
interactive gaming applications.

An example is the output of `gnuplot` with the command
```
export GNUTERM=sixel
gnuplot -e "splot [x=-3:3] [y=-3:3] sin(x) * cos(y)"
```

Note that gnuplot uses black text on default background for captions 
and coordinates so better not run it in a terminal with dark background.

### Image support ###

In addition to the legacy Sixel feature, mintty supports graphic image display 
(using iTerm2 controls). Image formats supported comprise
PNG, JPEG, GIF, TIFF, BMP, Exif.

The script `showimg` in the 
mintty [utils repository](https://github.com/mintty/utils) supports 
interactive image display.


## Tektronix 4014 vector graphics ##

Mintty can emulate the Tektronix 4014 vector graphics terminal. 
It switches to Tek emulation on the xterm sequence DECSET 38 (`\e[?38h`). 
It is suggested to adjust the window size to the Tektronix 4010 resolution and aspect ratio before:
* `echo -en "\e[4;780;1024t"`

The script `tek` in the mintty 
[utils repository](https://github.com/mintty/utils) supports switching 
to Tek mode and optionally output of Tek or plot files.
It also sets the environment variables **TERM** and **GNUTERM** properly.
When leaving the sub-shell, it restores DEC/ANSI terminal mode.


## Localization ##

Mintty facilitates localization of its user interface, the Options dialog, 
menus, message boxes, and terminal in-line error messages.
The localization language can be selected with the option `Language`, 
see manual page for details.

Example:
Assume setting `Language=*`, environment variables 
`LANGUAGE=de_CH:français:de:fr_FR` and `LC_MESSAGES=en_GB.UTF-8`, 
environment variable `LC_ALL` not set:
mintty tries to find localization files (in this order) for 
`de_CH`, `français`, `de`, `fr_FR`, `en_GB`, 
then (as generic fallback) `fr` and `en`, 
each in all resource configuration folders (subfolder `lang`).

Note that Windows may already have localized the default entries of the 
system menu, which makes the system menu language inconsistent because 
mintty adds a few items here. Choose `Language=en` or `en_US` to 
“reverse-localize” this, as well as the font and colour chooser dialogs.
Choose `Language=en_US` to change `Colour` to `Color` in the menus.

### Adding translations to localization ###

Localization files for various language or language/region codes 
are looked up in the resource configuration folders, subfolder `lang`.
Mintty uses a simplified `gettext` file format but not the `gettext` library;
all messages must be encoded in UTF-8, the Content-Type charset is ignored.

To add a new language, copy `messages.pot` to the desired `.po` file 
(including a region suffix if appropriate, like `fr_CH`) and add the 
`msgstr` entries which are empty in the template. The tool `poedit` may 
be used but remember to use UTF-8 encoding.
Check the translations for strings that may be too long and get clipped 
by a careful walkthrough of the Options dialog, opening all popups and 
sub-dialogs (colours and font) and also checking `mintty -o FontMenu=0`.

_Note:_ For setting values in popup menus of some of the options 
in the Options dialog, localization is also supported. Note however 
that the corresponding values in the config file or on the command line 
must not be localized, so apparent inconsistencies may arise.
It is therefore suggested _not_ to localize these values 
(marked “(localization optional)” in the localization template `messages.pot`).

_Note:_ There is one special pseudo-string in the localization template which 
facilitates scaling of the Options dialog width. It is labelled 
“__ Options: dialog width scale factor (80...200)” and its template value 
is “100”. If you provide a pseudo-translated value between 80 and 200 
for it, the Options dialog width will be scaled by that percentage.
(The navigation panel remains unscaled.)

Note that `&` marks in menu item labels define keyboard shortcuts to be 
handled by Windows. The script `keycheck` with your `.po` file as parameter 
checks for ambiguous shortcut entries; these are not errors but you may 
consider to reduce ambiguities. Note that a future (or currently patched) 
version of the `uniq` tool is needed to cover non-ASCII keyboard shortcuts.


## Character information display ##

Diagnostic display of current character information can be toggled 
from the extended context menu (Ctrl+right-click).
* _Unicode character codes_ at the current cursor position will then be displayed in the window title bar. (Note that mintty may precompose a combining character sequence into a combined character which is then displayed.)
* _Unicode character names_ will be included in the display if the **unicode-ucd** package is installed in `/usr/share` (or the file `charnames.txt` generated by the mintty script `src/mknames` is installed in the mintty resource subfolder `info`).
* _Emoji sequence “short names”_ will be indicated if Emojis display is enabled.
Note that the “normal” window title setting sequence 
and the character information output simply overwrite each other.


## User-defined behaviour ##

Mintty supports a few extension features:
* Application-specific drag-and-drop transformations (option `DropCommands`)
* User-defined commands and filters for context menu (option `UserCommands`)
* User-defined functions for key combinations (option `KeyFunctions`)
* User-defined function entries for system menu (option `SysMenuFunctions`)

See the manual page for details.

### Terminating the foreground program, the hard way ###

As an example for a user-defined command, that is not used for filtering 
text in this case, assume the user wants a menu option to terminate the 
terminal foreground process (in case it is stalled). This can be done by 
including a user command:

```
UserCommands=Kill foreground process:kill -9 $MINTTY_PID
```

### Terminating the foreground program, the smart way ###

With option `ExitCommands`, a set of strings (likely control strings) 
can be defined to be sent to the respective foreground application 
if the window is instructed to "Close" from its menu or close button.

_Note:_ Detection of terminal foreground processes works only locally; 
this features does not work with WSL or after remote login.

_Note:_ This feature potentially makes mintty vulnerable against command injection.
Be careful what strings you configure!

Example:
```
ExitCommands=bash:exit^M;mined:^[q;emacs:^X^C
```


## Running mintty stand-alone ##

To install mintty outside a cygwin environment, follow a few rules:
* Compile mintty statically.
* Install mintty.exe together with cygwin1.dll and cygwin-console-helper.exe.
* Call the directory in which to install mintty and libraries `bin` (optional).
* The parent directory of `bin` will be considered the mintty root directory.
* Aside the `bin` directory (in the root directory), install folder tree `usr/share/mintty` with subdirectories for mintty resources, e.g. `lang/*.po`, `themes/*`, `sounds/*` etc.

### Bundling mintty with dedicated software ###

To bundle an application which is not natively compiled on cygwin with mintty,
cygwin 3.1.0 provides the ConPTY support to bridge the terminal interworking incompatiblity problems 
([pty incompatibility problem](https://github.com/mintty/mintty/issues/56) and
[character encoding incompatibility problem](https://github.com/mintty/mintty/issues/376)).

For software that is aware of Posix terminal conventions, it may be a feasible 
solution if the software detects a terminal and its character encoding by 
checking environment variable `TERM` and the locale variables and invokes 
`stty raw -echo` to enable direct character-based I/O and disable 
non-compatible signal handling. For this purpose, stty and its library 
dependencies need to be bundled with the installation as well.

To run WSL, use `wslbridge2` as a gateway (see above).

