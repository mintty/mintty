

## Using shortcuts to start mintty ##

The Cygwin [setup.exe](http://cygwin.com/setup.exe) package for mintty installs a shortcut in the Windows start menu under _All Programs/Cygwin_. That starts mintty with a '-' (i.e. a single dash) as its only argument, which tells it to invoke the user's default shell as a login shell.

Shortcuts are also a convenient way to start mintty with additional options and different commands. For example, shortcuts for access to remote machines can be created by invoking **[ssh](http://www.openssh.com)**. The command simply needs to be appended to the target field of the shortcut's properties:

> Target: `C:\Cygwin\bin\mintty.exe /bin/ssh server`

The **[cygutils](http://www.cygwin.com/cygwin-ug-net/using-effectively.html#using-cygutils)** package provides the **mkshortcut** utility for creating shortcuts from the command line. See its manual page for details.


## Starting mintty from a batch file ##

In order to start mintty from a batch file it needs to be invoked through the **[start](http://technet.microsoft.com/en-us/library/cc770297.aspx)** command. This avoids the batch file's console window staying open while mintty is running. For example:

```
start mintty -
```

The console window for the batch file will still show up briefly, however. This can be avoided by invoking mintty from a shortcut instead, as described above.


## Creating a folder context menu entry for mintty ##

Cygwin's **chere** package can be used to create folder context menu entries in Explorer, which allow a shell to be opened with the working directory set to the selected folder.

The following command will create an entry called _Bash Prompt Here_ for the current user that will invoke bash running in mintty. See the chere manual (_man chere_) for all the options.

```
chere -i -c -t mintty
```


## Setting environment variables ##

Unfortunately Windows shortcuts do not allow the setting of environment variables. Variables can be set globally though via a button on the _Advanced_ tab of the system properties. Those can be reached by right-clicking on the _(My) Computer_ entry in the start menu or on the desktop, selecting _Properties_, then _Advanced System Settings_.

Alternatively, global variables can be set using the **[setx](http://technet.microsoft.com/en-us/library/cc755104.aspx)** command line utility. This comes pre-installed with some versions of Windows but is also available as part of the freely downloadable _Windows 2003 Resource Kit Tools_.

The **[env](http://www.opengroup.org/onlinepubs/9699919799/utilities/env.html)** utility can be used to set variables specifically for the program to be run in mintty, e.g.:

```
mintty /bin/env DISPLAY=:0 /bin/ssh -X server
```


## Starting in a particular directory ##

The working directory for a mintty session can be set in the _Start In_ field of a shortcut, or by changing directory in an invoking script. Note, however, that Cygwin's _/etc/profile_ script for login shells automatically changes to the user's home directory. The profile script can be told not to do this by setting a variable called _CHERE\_INVOKING_, like this:

```
mintty /bin/env CHERE_INVOKING=1 /bin/bash -l
```


## Terminal line settings ##

Terminal line settings can be viewed or changed with the **[stty](http://www.opengroup.org/onlinepubs/9699919799/utilities/stty.html)** utility, which is installed as part of Cygwin's core utilities package. Among other things, it can set the control characters used for generating signals or editing an input line.

See the stty manual for all the details, but here are a few examples. The commands can be included in shell startup files to make them permanent.

To change the key for deleting a whole word from _Ctrl+W_ to _Ctrl+Backspace_:

```
stty werase '^_'
```

To use _Ctrl+Enter_ instead of _Ctrl+D_ for end of file:

```
stty eof '^^'
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

Unix terminal line drivers have a flow control feature that allow terminal output to be stopped with _Ctrl+S_ and restarted with _Ctrl+Q_. However, due to the scrollback feature in modern terminal emulators, there is little need for this. Hence, to make those key combinations available for other uses, disable flow control with this command:

```
stty -ixon
```


## Readline configuration ##

Keyboard input for the **[bash](http://www.gnu.org/software/bash)** shell and other programs that use the **[readline](http://tiswww.case.edu/php/chet/readline/rltop.html)** library can be configured with the so-called **[inputrc](http://tiswww.case.edu/php/chet/readline/readline.html#SEC9)** file. Unless overridden by setting the _INPUTRC_ variable, this is located at _~/.inputrc_. It consists of bindings of keycodes to readline commands, whereby comments start with a hash character. See its manual for details.

Anyone used to Windows key combinations for editing text might find the following bindings useful:

```
# Ctrl+Left/Right to move by whole words
"\e[1;5C": forward-word
"\e[1;5D": backward-word

# Ctrl+Backspace/Delete to delete whole words
"\e[3;5~": kill-word
"\C-_": backward-kill-word

# Ctrl+Shift+Backspace/Delete to delete to start/end of the line
"\e[3;6~": kill-line
"\xC2\x9F": backward-kill-line  # for UTF-8
#"\x9F": backward-kill-line     # for ISO-8859-x
#"\e\C-_": backward-kill-line   # for any other charset

# Alt-Backspace for undo
"\e\d": undo
```

(The Ctrl+Shift+Backspace keycode depends on the selected character set, so the appropriate binding needs to be chosen.)

Finally, a couple of bindings for convenient searching of the command history. Just enter the first few characters of a previous command and press _Ctrl+Up_ to look it up.

```
# Ctrl+Up/Down for searching command history
"\e[1;5A": history-search-backward
"\e[1;5B": history-search-forward
```


## Mode-dependent cursor in vim ##

Mintty supports control sequences for changing cursor style. These can be used to configure **[vim](http://www.vim.org/)** such that the cursor changes depending on mode. For example, with the following lines in _~/.vimrc_, vim will show a block cursor in normal mode and a line cursor in insert mode:

```
let &t_ti.="\e[1 q"
let &t_SI.="\e[5 q"
let &t_EI.="\e[1 q"
let &t_te.="\e[0 q"
```


## Avoiding escape timeout issues in vim ##

It's a historical flaw of Unix terminals that the keycode of the escape key, i.e. the escape character, also appears at the start of many other keycodes. This means that on seeing an escape character, an application cannot be sure whether to treat it as an escape key press or whether to expect more characters to complete a longer keycode.

Therefore they tend to employ a timeout to decide. The delay on the escape key can be annoying though, particularly with the mode-dependent cursor above enabled.  The timeout approach can also fail on slow connections or a heavily loaded machine.

Mintty's "application escape key mode" can be used to avoid this by switching the escape key to an unambiguous keycode. Add the following to _~/.vimrc_ to employ it in vim:

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


## Using Ctrl+Tab to switch session in GNU Screen ##

The _Ctrl+Tab_ and _Ctrl+Shift+Tab_ key combinations can be used to switch session in **[GNU Screen](http://www.gnu.org/software/screen)**. In order to do do, their use as shortcuts for switching mintty windows needs to be disabled on the _Keys_ page of the options, and their keycodes need to be mapped in _~/.screenrc_:

```
bindkey "^[[1;5I" next
bindkey "^[[1;6I" prev
```


## Compose key ##

Mintty uses the Windows keyboard layout system with its "dead key" mechanism for entering accented characters. X11, on the other hand, has the compose key mechanism for this purpose. The open source **[AllChars](http://allchars.zwolnet.com)** utility can be used to emulate that approach on Windows.


## Changing colours ##

The default foreground, background and cursor colours can be changed in the options dialog, or by specifying the _ForegroundColour_, _BackgroundColour_ and _CursorColour_ settings in the configuration file or on the command line.

However, they can also be changed from within the terminal using the xterm control sequences for this purpose, for example:

```
echo -ne '\e]10;#000000\a'  # Black foreground
echo -ne '\e]11;#C0C0C0\a'  # Light gray background
echo -ne '\e]12;#00FF00\a'  # Green cursor
```

In mintty, the RGB colour values can also be specified using a comma-separated decimal notation, for example `255,0,0` instead of `#FF0000` for red. [X11 colour names](http://en.wikipedia.org/wiki/X11_color_names) are not currently supported though.

The 16 [ANSI colours](http://en.wikipedia.org/wiki/ANSI_escape_code#Colors) can be set in the configuration file or on the command line using settings such as _Blue_ or _BoldMagenta_. These are documented in the [configuration section](http://mintty.googlecode.com/svn/trunk/docs/mintty.1.html#27) of the manual. They can also be changed using xterm control sequences. Here they are with their default values:

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