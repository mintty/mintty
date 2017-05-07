

## Configuring mintty ##

Mintty supports a number of common places to look for and save its 
configuration and resources.

For its configuration file, it reads ```/etc/minttyrc```, ```$APPDATA/mintty/config```, 
```~/.config/mintty/config```, ```~/.minttyrc```, in this order.

For resource files to configure a colour scheme, 
wave file for the bell character, or localization files, it looks for 
subfolders ```themes```, ```sounds```, or ```lang```, respectively, 
in one of the directories ```~/.mintty```, ```~/.config/mintty```, 
```$APPDATA/mintty```, ```/usr/share/mintty```, whichever is found first.

The ```~/.config/mintty``` folder is the XDG default base directory.
The ```$APPDATA/mintty``` folder is especially useful to share common configuration 
for various installations of mintty (e.g. cygwin 32/64 Bit, MSYS, Git Bash).


## Using desktop shortcuts to start mintty ##

The Cygwin [setup.exe](http://cygwin.com/setup.exe) package for mintty 
installs a shortcut in the Windows start menu under _All Programs/Cygwin_.
It starts mintty with a '-' (i.e. a single dash) as its only argument, 
which tells it to invoke the user's default shell as a login shell.

Shortcuts are also a convenient way to start mintty with additional options and different commands. 
For example, shortcuts for access to remote machines can be created by 
invoking **[ssh](http://www.openssh.com)**. The command simply needs 
to be appended to the target field of the shortcut's properties:

> Target: `C:\Cygwin\bin\mintty.exe /bin/ssh server`

The **[cygutils](http://www.cygwin.com/cygwin-ug-net/using-effectively.html#using-cygutils)** package 
provides the **mkshortcut** utility for creating shortcuts from the command line. 
See its manual page for details.

A Windows shortcut can be associated with a **Shortcut key** so an instance 
of mintty can be started using a hotkey. By default, an already running 
instance would be focussed again with the associated hotkey. To have a 
new instance started with every usage of the hotkey, use the command-line 
option ```-D``` for mintty in the shortcut target.

_Note:_ About interaction problems of icon, shortcut, and the Windows taskbar:
In a Windows desktop shortcut, to achieve consistent icon behaviour, 
the same icon should be specified in the shortcut properties (Change Icon...) 
and the mintty command line (Target:),
or (beginning 2.2.3) no icon should be specified on the command line as 
mintty will then take the icon from the invoking shortcut.

_Note:_ It is suggested to _not_ use the option AppID in a Windows desktop 
shortcut, or follow the advice about avoiding trouble with taskbar grouping 
in the manual page.


## Using mintty for Bash on Ubuntu on Windows (UoW) / Windows Subsystem for Linux (WSL) ##

Install [wsltty](https://github.com/mintty/wsltty).

Or, to help reproduce the installation manually, for users of cygwin or msys2:
* From https://github.com/rprichard/wslbridge/releases, download the `wslbridge` archive corresponding to your system (cygwin/msys2 32/64 bit)
* Install `wslbridge.exe` and `wslbridge-backend` into your cygwin or msys2 `/bin` directory
* Make a desktop shortcut (Desktop right-click – New ▸ Shortcut) with 
 * Target: `X:\cygwin64\bin\mintty.exe /bin/wslbridge.exe -t /bin/bash -l`
 * Icon location (Change Icon…): `%LOCALAPPDATA%\lxss\bash.ico`

Replace ```X:\cygwin64``` with your cygwin or msys2 root directory path.
The “Start in:” directory does not normally matter if you include the bash “login” option 
```-l``` as shown above and have a typical Linux profile configuration.
You may replace ```/bin/bash``` above with your favourite shell if desired.


## Starting mintty from a batch file ##

In order to start mintty from a batch file it needs to be invoked through the **[start](http://technet.microsoft.com/en-us/library/cc770297.aspx)** command. This avoids the batch file's console window staying open while mintty is running. For example:

```
start mintty -
```

The console window for the batch file will still show up briefly, however. This can be avoided by invoking mintty from a shortcut instead, as described above.


## Starting in a particular directory ##

The working directory for a mintty session can be set in the _Start In_ field of a shortcut, 
or by changing directory in an invoking script, or with option `--dir`.
Note, however, that Cygwin's _/etc/profile_ script for login shells automatically changes to the user's home directory.
The profile script can be told not to do this by setting a variable called _CHERE\_INVOKING_, like this:

```
mintty /bin/env CHERE_INVOKING=1 /bin/bash -l
```


## Creating a folder context menu entry for mintty ##

Cygwin's **chere** package can be used to create folder context menu entries in Explorer, which allow a shell to be opened with the working directory set to the selected folder.

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
effects are observed; this is the 
[pty incompatibility problem](https://github.com/mintty/mintty/issues/56) 
and the 
[character encoding incompatibility problem](https://github.com/mintty/mintty/issues/376).
This basically affects all programs not compiled in a cygwin or msys 
environment (and note that MinGW is not msys in this context).

As a workaround, you can use [winpty](https://github.com/rprichard/winpty) 
as a wrapper to invoke the Windows program.

_Note:_ There is no point in reporting this for the 15th time as a mintty 
issue, because it is not a mintty issue (or well, an issue maybe, but not 
caused by, or fixable by, mintty); it is a generic problem of cygwin/msys 
and occurs likewise in all other pty-based terminals (e.g. xterm).


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

Mintty's “application escape key mode” can be used to avoid this by switching the escape key to an unambiguous keycode. Add the following to _~/.vimrc_ to employ it in vim:

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


## Keyboard not working as expected in certain applications (e.g. vim) ##

If for example the PgUp and PgDn keys do not work in your editor, the reason 
may be that in the mintty Options, the Terminal Type was set to "vt100" 
and based on the resulting setting of the environment variable TERM, 
the application expects other key sequences than mintty sends.
(While mintty could be changed to send VT100 application keypad codes in 
that case, the current behaviour is compatible with xterm.)


## Using Ctrl+Tab to switch session in GNU Screen ##

The _Ctrl+Tab_ and _Ctrl+Shift+Tab_ key combinations can be used to switch session in **[GNU Screen](http://www.gnu.org/software/screen)**. In order to do do, their use as shortcuts for switching mintty windows needs to be disabled on the _Keys_ page of the options, and their keycodes need to be mapped in _~/.screenrc_:

```
bindkey "^[[1;5I" next
bindkey "^[[1;6I" prev
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


## Changing colours ##

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
* _color-name_ (using X11 color names, e.g. ```echo -ne '\e]10;bisque2\a'```)


## Using colour schemes (“Themes”) ##

Colour schemes (that redefine ANSI colours and possibly foreground/background 
colours) can be loaded with the option ```-C``` (capital C) or ```--loadconfig``` 
which loads a configuration file read-only, i.e. configuration changes 
are not saved to this file, or with the new setting _ThemeFile_.

In the Options menu, section _Looks_, the _Theme_ popup offers theme files 
as stored in a resource directory for selection.
This dialog field can be used in different ways:
* Popup the selection to choose a theme configured in your resource directory
* Insert a file name (e.g. by pasting or drag-and-drop from Windows Explorer)
* Drag-and-drop a theme file from the Internet (may be embedded in HTML page)
* Drag-and-drop a colour scheme directly from the Color Scheme Designer (see below)

Note that the drag-and-drop theme file option needs the program ```curl```
to be installed (and properly configured for a proxy if needed).

After drag-and-drop of a colour scheme, you may Apply it for testing;
to keep the scheme in your popup selection, assign a name to it by typing it 
into the Theme field, then click the “Store” button. After downloading a 
theme file, the name will be filled with its basename as a suggestion.
As long as a colour scheme is loaded but not yet stored, and a name is 
available in the Theme field, the “Store” button will be enabled.

There is an excellent colour scheme designer available:
[4bit Terminal Color Scheme Designer](http://ciembor.github.io/4bit/#) 
which lets you download a tuned colour scheme (top-right button “Get Scheme”).
Click on the button “Color Scheme Designer” below the Theme field 
to open the designer page and start your design. You can either download 
the scheme file (“Get Scheme” – “mintty”) or drag-and-drop the download link 
directly to the mintty Options menu, to either the Theme field or the 
Color Scheme Designer button. You can then click Apply to test the design 
and if you like it, you can enter a theme name in the Theme field and then 
click the “Store” button to store the colour scheme.

A number of colour schemes have been published for mintty, e.g.
* https://github.com/oumu/mintty-color-schemes
* https://github.com/mavnn/mintty-colors-solarized
* https://github.com/PhilipDaniels/mintty/tree/master/themes


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

The old font selection and menu format can be chosen with setting OldFontMenu.

If you are missing certain characters, e.g. as used for the popular “Powerline” plugin,
the reason may be that specifically designed characters are being addressed 
that are provided in the Unicode Private Use range of dedicated fonts;
a collection of such fonts can be found at [Nerd Fonts](http://nerdfonts.com/).


## Ambiguous width setting ##

A number of Unicode characters have an “ambiguous width” property due to 
legacy issues with dedicated CJK fonts, meaning they can be narrow 
(single-cell width) or wide (double-cell width) in a terminal.

To select ambiguous-width characters to appear wide (as some applications 
may expect), mintty should be run in a CJK locale (character encoding does 
not need to be CJK), e.g.:

```
LC_CTYPE=zh_SG.utf8 mintty &
```


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
* Row height and column width can furthermore be tuned with setting `RowSpacing` and `ColSpacing`.
* A gap between text and window border can be specified with setting `Padding` (default 1).

Note: The term “leading” (pronounced like “ledding”) comes from the 
times of metal typesetting when strips of lead (the metal) were used 
to adjust line spacing.

### Character width ###

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


## Text attributes and rendering ##

Mintty supports a maximum of usual and unusual text attributes:

| **start `^[[...m`**    | **end `^[[...m`** | **attribute**                 |
|:-----------------------|:------------------|:------------------------------|
| 1                      | 22                | bold                          |
| 2                      | 22                | dim                           |
| 3                      | 23                | italic                        |
| 4                      | 24                | underline                     |
| 5                      | 25                | blinking                      |
| 7                      | 27                | inverse                       |
| 8                      | 28                | invisible                     |
| 9                      | 29                | strikeout                     |
| 21                     | 24                | doubly underline              |
| 53                     | 55                | overline                      |
| 30...37                | 39                | foreground ANSI colour        |
| 90...97                | 39                | foreground bright ANSI colour |
| 40...47                | 49                | background ANSI colour        |
| 100...107              | 49                | background bright ANSI colour |
| 38;5;P                 | 39                | foreground palette colour     |
| 48;5;P                 | 49                | background palette colour     |
| 38;2;R;G;B             | 39                | foreground true colour        |
| 48;2;R;G;B             | 49                | background true colour        |
| _any_                  | 0                 |                               |

As a fancy add-on feature for text attributes, mintty supports distinct 
colour attributes for combining characters, so a combined character 
can be displayed in multiple colours. Attributes considered for this 
purpose are default and ANSI foreground colours, palette and true-colour 
foreground colours, dim mode and manual bold mode (BoldAsFont=false); 
background colours and inverse mode are ignored.
<img src=https://github.com/mintty/mintty/wiki/mintty-coloured-combinings.png>


## Passing arguments from an environment with different character set ##

To pass non-ASCII parameters to a command run from mintty using a specific 
character encoding, proper conversion must be crafted.
See [issue #463](https://github.com/mintty/mintty/issues/463) 
for a discussion.
For example, for a desktop shortcut to start a GBK-encoded mintty 
starting in a specific directory with a non-ASCII name, 
use this command line as a shortcut target:

```
C:\cygwin\bin\mintty.exe -o Locale=C -o Charset=GBK /bin/bash -l -c "cd `echo D:/桌面 | iconv -f UTF-8`; exec bash"
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

The new support of the SIXEL feature facilitates a range of applications 
that integrate graphic images in the terminal, animated graphics, and even 
video and interactive gaming applications.

An example of the benefit of this feature is the output of `gnuplot` 
with the command
```
export GNUTERM=sixel
gnuplot -e "splot [x=-3:3] [y=-3:3] sin(x) * cos(y)"
```

Note that gnuplot uses black text on default background for captions 
and coordinates so better not run it in a terminal with dark background.


## Localization ##

Mintty facilitates localization of its user interface, the Options dialog, 
menus, message boxes, and terminal in-line error messages.
The localization language can be selected with the option `Language`, 
see manual page for details.

Example:
`Language=*`, environment variables `LANGUAGE=de_CH:français:de:fr_FR` and 
`LC_MESSAGES=en_GB.UTF-8`, `LC_ALL` not set:
mintty tries to find localization files (in this order) for 
`de_CH`, `français`, `de`, `fr_FR`, `en_GB`, 
then (as generic fallback) `fr` and `en`, 
each in all resource configuration folders (subfolder `lang`).

Note that Windows may already have localized the default entries of the 
system menu, which makes the system menu language inconsistent because 
mintty adds a few items here. Choose `Language=en` to 
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
Note that the “normal” window title setting sequence 
and the character information output simply overwrite each other.


## User-defined behaviour ##

Mintty supports two extension features:
* Application-specific drag-and-drop transformations (option `DropCommands`)
* User-defined commands and filter functions (option `UserCommands`)

See the manual page for details.


## Running mintty stand-alone ##

To install mintty outside a cygwin environment, follow a few rules:
* Find out which libraries (dlls from the cygwin /bin directory) mintty needs in addition to cygwin1.dll and install them all, or:
* Compile mintty statically.
* Call the directory in which to install mintty and libraries 'bin'.

