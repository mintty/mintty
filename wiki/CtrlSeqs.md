

## Introduction ##

Mintty's terminal emulation is aimed at compatibility with 
**[xterm](http://invisible-island.net/xterm/xterm.html)**. 
Most of the xterm control sequences documented at 
http://invisible-island.net/xterm/ctlseqs/ctlseqs.html are supported. 
Please [report as bugs](https://github.com/mintty/mintty/issues) 
any incompatibilities or unimplemented sequences that would be useful.

Some sequences that were introduced by other terminals such as the 
[Linux console](http://www.kernel.org/doc/man-pages/online/pages/man4/console_codes.4.html), 
and that aren't available in xterm, are also supported.

This page only lists control sequences that are specific to mintty. 
[Caret notation](http://en.wikipedia.org/wiki/Caret_notation) is used to show control characters. 
The full details of all supported control sequences are only available in the 
[source code](https://github.com/mintty/mintty/blob/master/src/termout.c).


## Terminal identification ##

These escape sequences cause mintty to report its identification.

| **request** | **response**                      | **comment** |
|:------------|:----------------------------------|:------------|
| `^[[>0c`    | `^[[>77;`_version_`;`_unicode_`c` | secondary devices attributes (DEC); _version_ like 30105, _unicode_ version when using built-in data |
| `^[[>0q`    | `^[P>\|mintty `_version_`^[\`     | terminal identification query (xterm 354); _version_ like 3.1.5 |


## Escape keycode ##

There are two settings controlling the keycode sent by the [Esc key](http://en.wikipedia.org/wiki/Esc_key).

The first controls application escape key mode, where the escape key sends a keycode that allows applications such as **[vim](http://www.vim.org)** to tell it apart from the escape character appearing at the start of many other keycodes, without resorting to a timeout mechanism.

| **sequence**  | **mode**      | **keycode**    |
|:--------------|:--------------|:---------------|
| `^[[?7727l`   | normal        | `^[` or `^\`   |
| `^[[?7727h`   | application   | `^[O[`         |

When application escape key mode is off, the escape key can be be configured to send `^\` instead of the standard `^[`. This allows the escape key to be used as one of the special keys in the terminal line settings (as set with the **[stty](http://www.opengroup.org/onlinepubs/009695399/utilities/stty.html)** utility).

| **sequence**  | **keycode** |
|:--------------|:------------|
| `^[[?7728l`   | `^[`        |
| `^[[?7728h`   | `^\`        |


## Control key codes ##

Application control key mode can be configured per control key.
It facilitates more distinction between different keys, as well as 
usage of Ctrl+[ in various applications that would normally handle 
the ESC character as a generic key code prefix.
Possible distinctions:
  * Esc key and Ctrl+[ key
  * Tab character and Ctrl+I key
  * NUL character sent by Ctrl+space and NUL key code sent by Ctrl+@

As a generic feature, this configuration is accepted for all control 
characters. Generated key codes are similar to those sent in the xterm 
modifyOtherKeys mode, but normalized to a contiguous range of codes using 
capital ASCII character codes, and indicating the control modifier only.
Settings can be combined in a common sequence like `^[[?77009;77027h`.
The respective setting is cleared with a corresponding sequence ending with `l`.

| **sequence**  | **input** | **key code** |
|:--------------|:----------|:-------------|
| `^[[?77000h`  | Ctrl+@    | `^[[64;5u`   |
| ...           |           |              |
| `^[[?77009h`  | Ctrl+I    | `^[[73;5u`   |
| ...           |           |              |
| `^[[?77027h`  | Ctrl+[    | `^[[91;5u`   |
| ...           |           |              |
| `^[[?77031h`  | Ctrl+_    | `^[[95;5u`   |


## Input method ##

Mintty allows to set, save or restore the IME status explicitly, to support 
applications like text editors to adapt it to the current input target.

| **sequence**  | **IME status**                            |
|:--------------|:------------------------------------------|
| `^[[<`_on_`t` | Open (1) or Close (0, default) IME status |
| `^[[<s`       | Save (push) IME status                    |
| `^[[<r`       | Restore (pop) IME status                  |


## Shortcut override mode ##

When shortcut override mode is on, all shortcut key combinations are sent to the application instead of triggering window commands.

| **sequence**  | **override** |
|:--------------|:-------------|
| `^[[?7783l`   | off          |
| `^[[?7783h`   | on           |


## Keyboard auto repeat ##

With the VT520 sequence DECARR the keyboard auto-repeat speed can be 
limited to the given value in characters per second.
Unlike original DECARR, a value of 0 disables repeat rate limitation.
Keyboard auto-repeat can also be disabled with DECSET 8 (DECARM).

| **sequence**   | **comment**         |
|:---------------|:--------------------|
| `^[[`_cps_`-p` | max 30              |
| `^[[-p       ` | unlimited           |
| `^[[?8l`       | disable auto-repeat |
| `^[[?8h`       | enable auto-repeat  |


## Area attributes change ##

Mintty extends the scope of rectangular area attributes change functions 
DECCARA and DECRARA to additional attributes as suitable.
Colour and font changing functions are only supported with DECCARA.
True colour and underline colour settings are not supported.
Examples:

| **sequence**             | **function**                                    |
|:-------------------------|:------------------------------------------------|
| `^[[2*x`                 | set rectangular area extent                     |
| `^[[`_Pt_`;`_Pl_`;`_Pb_`;`_Pr_`;3;38:5:20$r` | set italic and palette colour 20 in area |
| `^[[`_Pt_`;`_Pl_`;`_Pb_`;`_Pr_`;29$r` | clear strikeout in area |
| `^[[`_Pt_`;`_Pl_`;`_Pb_`;`_Pr_`;9$t` | revert strikeout in area |


## Unscroll ##

This sequence scrolls down screen lines like SD (CSI T) but fills the 
empty lines from the end of the scrollback buffer.

> `^[[`_N_`+T`


## Status line / area ##

Mintty implements the DEC VT320 status line and extends the feature to 
support a multi-line host-writable status area.
It is configured with a proprietary second parameter to DECSSDT 2.
Its height is limited to be smaller than half the screen height.

| **sequence**          | **function**                              |
|:----------------------|:------------------------------------------|
| `^[[0$~` _or_ `^[[$~` | disable status line                       |
| `^[[1$~`              | enable indicator status line              |
| `^[[2$~`              | enable host-writable status line          |
| `^[[2;`_N_`$~`        | enable host-writable status area, N lines |
| `^[[0$}` _or_ `^[[$}` | select normal display (for writing)       |
| `^[[1$}`              | select status display (for writing)       |


## Bidirectional rendering ##

Mintty supports bidi rendering by default. However, some applications 
may prefer to control bidi appearance themselves. There is one option (Bidi) 
and some control sequences to adjust the behaviour.

| **option**  | **bidi**     |
|:------------|:-------------|
| `Bidi=0`    | disabled     |
| `Bidi=1`    | disabled on alternate screen |
| `Bidi=2`    | (default) enabled |

| **sequence**  | **bidi**     |
|:--------------|:-------------|
| `^[[?77096h`  | disabled     |
| `^[[?77096l`  | enabled      |
| `^[[?7796h`   | disabled on current line |
| `^[[?7796l`   | not disabled on current line |
| `^[[8h`       | BDSM (ECMA-48): implicit bidi mode (bidi-enabled lines) |
| `^[[8l`       | BDSM (ECMA-48): explicit bidi mode (bidi-disabled lines) |
| `^[[?2501h`   | enable bidi autodetection (default) |
| `^[[?2501l`   | disable bidi autodetection |
| `^[[1 k`      | SCP (ECMA-48): set lines to LTR paragraph embedding level |
| `^[[2 k`      | SCP (ECMA-48): set lines to RTL paragraph embedding level |
| `^[[0 k`      | SCP (ECMA-48): default direction handling: autodetection with LTR fallback |
| `^[[?2500h`   | enable box mirroring (*) |
| `^[[?2500l`   | disable box mirroring (*) |
| `^[[0 S`      | SPD (ECMA-48): LTR presentation direction |
| `^[[3 S`      | SPD (ECMA-48): RTL presentation direction |

Note: ECMA-48 bidi modes and private bidi modes are experimental.
They follow the current status of the bidi mode model of the 
[«BiDi in Terminal Emulators» recommendation](https://terminal-wg.pages.freedesktop.org/bidi/).

Note: Box mirroring means a number of graphic characters are added to the 
set of bidi-mirrored characters as specified by Unicode.
These are the unsymmetric characters from ranges Box Drawing (U+2500-U+257F) 
and Block Elements (U+2580-U+259F). Others may be added in future versions.

Note: SPD is a deprecated fun feature.


## Reflow / Rewrap / Line rebreaking on resize ##

Mintty supports reflow of wrapped lines if the terminal is resized and its 
width is changed. This feature, applicable with setting `RewrapOnResize`, 
can be disabled per line, usable for example for prompt lines.

| **sequence**  | **rewrap on resize** |
|:--------------|:---------------------|
| `^[[?2027l`   | disabled             |
| `^[[?2027h`   | enabled (default)    |


## Scrollbar hiding ##

These sequences can be used to hide or show the scrollbar, whereby the window size remains the same but the number of character columns is changed to account for the width of the scrollbar. If the scrollbar is disabled in the options, it will always remain hidden.

| **sequence**  | **scrollbar** |
|:--------------|:--------------|
| `^[[?7766l`   | hide          |
| `^[[?7766h`   | show          |

Note: Mintty also supports the xterm-compatible sequences to hide or show 
the scrollbar, which handle the scrollbar as "outer" to the terminal, 
adding to the window width but keeping the terminal width unchanged 
(except in full-screen mode).


## Application scrollbar ##

— EXPERIMENTAL —

In application scrollbar mode, an application can make use of the window scrollbar;
it can set up the scrollbar to reflect the application idea of a scroll 
position, and receive scrollbar events as control sequences.

This mode is up to future revision. It is currently enabled or disabled 
implicitly, there is no explicit mode setting sequence.

The application scrollbar indicates a scrollbar view (scroll offset _position_) 
within an assumed span of a virtual document (document _size_, as 
maintained by the application). The height of the view (viewport _height_) 
defaults to the actual terminal size (rows); its difference to the 
terminal size is kept when resizing the terminal.

Control sequences can set the current view position (scroll offset _position_ 
of the top end of the marked area in the scrollbar, 
from 1 to _size_ − _height_ + 1) as well as the total virtual document _size_ 
(in assumed lines) and optionally the viewport _height_.

| **sequence**                       | **scrollbar**                                        |
|:-----------------------------------|:-----------------------------------------------------|
| `^[[`_pos_`;`_size_`;`_height_`#t` | set scrollbar view position, virtual size and height |
| `^[[`_pos_`;`_size_`#t`            | set scrollbar view position and virtual size         |
| `^[[`_pos_`#t`                     | set scrollbar view position                          |
| `^[[0#t`                           | disable application scrollbar                        |

Relative scrollbar movement and absolute positioning are reported with 
special sequences; for details see 
[Keycodes – Application scrollbar events](https://github.com/mintty/mintty/wiki/Keycodes#application-scrollbar-events).
See there also for an illustrated explanation of the meaning of _pos_ vs _size_ values.


## Progress bar ##

— EXPERIMENTAL —

A progress indication on the taskbar icon can be switched or controlled with 
this escape sequence.
With a second parameter, the progress value can be controlled explicitly.
With only one parameter, automatic progress detection is enabled, 
scanning the current cursor line for a percentage indication (x%) and 
enabled by a subsequent relative positioning (e.g. a CR return 
character) like in text progress indications.
Note that automatic progress bar can also be configured (option ProgressBar).

| **sequence**                 | **comment**                                 |
|:-----------------------------|:--------------------------------------------|
| `^[[0%q`                     | disable progress indication                 |
| `^[[1%q`                     | enable progress indication level 1 (green)  |
| `^[[2%q`                     | enable progress indication level 2 (yellow) |
| `^[[3%q`                     | enable progress indication level 3 (red)    |
| `^[[10%q`                    | reset progress indication as configured     |
| `^[[`_level_`;`_percent_`%q` | set progress level (1..3) and value         |
| `^[[;`_percent_`%q`          | change progress value only                  |
| `^[[8%q`                     | enable continuous "busy" indication         |

An _OSC 9;4_ sequence (compatible with ConEmu or Windows Terminal) 
is available too, alternatively supporting mnemonic parameters.
It supports additional values to switch the progress scan mode 
(option `ProgressScan`).

| **sequence**                            | **comment**                       |
|:----------------------------------------|:----------------------------------|
| `^[]9;progress;off^G`                   | disable progress indication       |
| `^[]9;progress;green^G`                 | enable green progress indication  |
| `^[]9;progress;yellow^G`                | enable yellow progress indication |
| `^[]9;progress;red^G`                   | enable red progress indication    |
| `^[]9;progress;default^G` _or empty_    | reset progress indication         |
| `^[]9;progress;`_level_`;`_percent_`^G` | set progress level and value      |
| `^[]9;progress;busy^G`                  | enable busy indication            |
| `^[]9;progress;single^G`                | single-line scan mode             |
| `^[]9;progress;multiple^G`              | multi-line scan mode              |


## Mousewheel reporting ##

Mintty includes support for sending mousewheel events to an application without having to enable full [xterm mouse tracking](http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#Mouse%20Tracking), which takes over all mouse events and isn't supported by every application.

Mousewheel reporting only happens on the alternate screen, whereas on the primary screen, the mousewheel scrolls the scrollback buffer. The following two sequences enable or disable mousewheel reporting. It is enabled by default.

| **sequence**  | **reporting** |
|:--------------|:--------------|
| `^[[?1007l`   | disabled      |
| `^[[?1007h`   | enabled       |
| `^[[?7786l`   | disabled      |
| `^[[?7786h`   | enabled       |

The xterm-style sequence mode (1007) is disabled by default but the mintty 
feature (7786) is enabled by default. The mintty mode can be formatted 
to private sequences (see below). To support these subtle differences, 
both can be switched independently.

By default, mousewheel events are reported as cursor key presses, which enables
mousewheel scrolling in applications such as **[less](http://www.greenwoodsoftware.com/less)** without requiring any configuration. Alternatively, mousewheel reporting can be switched to _application mousewheel mode_, where the mousewheel sends its own separate keycodes that allow an application to treat the mousewheel differently from cursor keys:

| **event**   | **code**    |
|:------------|:------------|
| line up     | `^[Oa`      |
| line down   | `^[Ob`      |
| page up     | `^[[1;2a`   |
| page down   | `^[[1;2b`   |

Application mousewheel mode is controlled by these sequences:

| **sequence**  | **mode**      |
|:--------------|:--------------|
| `^[[?7787l`   | cursor        |
| `^[[?7787h`   | application   |


## Readline mouse modes ##

These three mode settings, switched by DECSET/DECRST sequences (xterm 379) 
enable mouse-controlled editing on the command line (as detected by the 
cursor position) by sending virtual cursor or erase keystrokes.

| **sequence**  | **mode**                                                  |
|:--------------|:----------------------------------------------------------|
| `^[[?2001h`   | left button places cursor on command line                 |
| `^[[?2002h`   | middle button pastes at current mouse position            |
| `^[[?2003h`   | double right-click deletes selection until mouse position |

Note that middle and right button functions only apply if the middle button 
is configured to Paste (default) and the right button is configured to 
Extend the selection, respectively.
The respective DECRST sequences (ending with `l`) disable the corresponding mode.
These modes can be preset with option `ClicksPlaceCursor` initially and on hard reset.


## Ambiguous width reporting ##

Applications can ask to be notified when the width of the so-called [ambiguous width](http://unicode.org/reports/tr11/#Ambiguous) character category changes due to the user changing font.

| **sequence**  | **reporting** |
|:--------------|:--------------|
| `^[[?7700l`   | disabled      |
| `^[[?7700h`   | enabled       |

When enabled, `^[[1W` is sent when changing to an "ambiguous narrow" font and `^[[2W` is sent when changing to an "ambiguous wide" font.


## Font change reporting ##

Applications can ask to be notified when the font has been changed.

| **sequence**  | **reporting** |
|:--------------|:--------------|
| `^[[?7767l`   | disabled      |
| `^[[?7767h`   | enabled       |

When enabled, `^[[0W` is sent when the font has changed, 
unless ambiguous width reporting is enabled too, in which case 
either `^[[1W` or `^[[2W` is sent as described above;
so if both reporting modes are enabled, only one report is sent.


## Font glyph coverage enquiry ##

Fonts vary widely in their Unicode coverage, i.e. they usually miss glyphs for many characters. The following sequence can be used to enquire about support for a specified list of characters.

> `^[]7771;?;`_char0_`;`_char1_...`^G`

Characters shall be specified with their decimal Unicode codepoint. Any number of characters can be given. Mintty replies with the same sequence, except that the question mark is replaced with an exclamation mark and that codes for characters that the current font does not have a glyph for are omitted.


## Wide characters ##

The following _OSC_ ("operating system command") sequences can be used to 
change wide display modes for Indic and a range of long Unicode characters:

| **sequence**           | **wide characters** |
|:-----------------------|:--------------------|
| `^[]77119;1^G`         | Indic               |
| `^[]77119;2^G`         | Long Unicode chars  |
| `^[]77119;0^G`         | none                |

Setting wide Indic mode, Indic characters with glyphs wider than a 
single character cell will be displayed in double-width (like CJK characters).
Note: This is a switchable option only as there is no authoritative 
source of information about which Indic characters should be considered wide;
most screen applications will not cooperate with this feature as their 
assumption of character widths is mostly based on the system locale 
(with the notable exception of the Unicode editor MinEd which supports 
Indic wide display).

Setting wide long Unicode characters mode, a number of Unicode characters 
that are supposed to be "wide" or "long" will be displayed 
in double-width (like CJK characters).
See comment above about cooperating applications.
The list of long Unicode characters considered "wide" is 
U+2001, U+2003, U+2014, U+27DD..U+27DE, U+27F5..U+27FF, 
U+2910, U+296A..U+296D, U+2B33, U+2E0E..U+2E11, U+2E3A..U+2E3B;
this list is subject to change in future versions.


## Explicit character width ##

— EXPERIMENTAL —

Mintty provides explicit width override as a character attribute, 
so an application can enforce single-width characters to be rendered wide 
or double-width ("wide") characters to be rendered narrow.
Experimentally, for this purpose the ECMA-48 escape sequences 
"Presentation Expand Or Contract" (PEC) `CSI` _num_ `SP Z` are used, 
with one extension:

| **sequence** | **effect**                                    |
|:-------------|:----------------------------------------------|
| `^[[0 Z`     | default: normal single or double width        |
| `^[[1 Z`     | expand: enforce double-cell display           |
| `^[[2 Z`     | contract: enforce single-cell display         |
| `^[[22 Z`    | zoom down to single-cell display (like setting `Charwidth=single`) |
| `^[[2;2 Z`   | like `^[[22 Z`                                |


## Overstrike ##

Mintty supports overstriking characters, with either an SGR attribute 
or the VK100-compatible DECSET 20.

| **sequence** | **effect**                                    |
|:-------------|:----------------------------------------------|
| `^[[8:7m`    | overstriking character writing mode           |
| `^[[28m`     | overwriting character writing mode            |
| `^[[?20h`    | overstriking character writing mode           |
| `^[[?20l`    | overwriting character writing mode            |


## Font size ##

The following _OSC_ ("operating system command") sequences can be used to change and query font size:

| **sequence**           | **font size**       |
|:-----------------------|:--------------------|
| `^[]7770;?^G`          | query               |
| `^[]7770;`_num_`^G`    | set to _num_        |
| `^[]7770;+`_num_`^G`   | increase by _num_   |
| `^[]7770;-`_num_`^G`   | decrease by _num_   |
| `^[]7770;^G`           | default             |

As usual, OSC sequences can also be terminated with `^[\` (_ST_, the string terminator) instead of `^G`.
When the font size is queried, a sequence that would restore the current font size is sent.


## Font and window size ##

The following _OSC_ ("operating system command") sequences can be used to change and query font size:

| **sequence**           | **font size**       |
|:-----------------------|:--------------------|
| `^[]7777;?^G`          | query               |
| `^[]7777;`_num_`^G`    | set to _num_        |
| `^[]7777;+`_num_`^G`   | increase by _num_   |
| `^[]7777;-`_num_`^G`   | decrease by _num_   |
| `^[]7777;^G`           | default             |

The window size is adapted to zoom with the font size, so the terminal character geometry is kept if possible.
As usual, OSC sequences can also be terminated with `^[\` (_ST_, the string terminator) instead of `^G`.
When the font size is queried, a sequence that would restore the current font and window size is sent.


## Font style ##

OSC 50 semantics is extended to alternative fonts and the Tek mode font;
it changes the font that is currently selected (and keeps that setting).


## Emojis style ##

Like OSC 50 for font style, this sequence can change the emojis style.
For values, see setting `Emojis` in the manual.

> `^[]7750;_emojis-style_`^G`


## Background image ##

OSC 11 semantics is extended to set or change image background.

| **sequence**         | **locale**                                        |
|:---------------------|:--------------------------------------------------|
| `^[]11;_`_image_`^G` | set terminal size background image                |
| `^[]11;%`_image_`^G` | set image and scale terminal to its aspect ratio  |
| `^[]11;*`_image_`^G` | set tiled background                              |
| `^[]11;+`_image_`^G` | set background scaled with aspect ratio and tiled |
| `^[]11;=^G`          | set background to desktop image (if tiled)        |

If the background filename is followed by a comma and a number between 1 and 254, 
the background image will be dimmed towards the background colour;
with a value of 255, the alpha transparency values of the image will be used.


## Locale ##

The locale and charset used by the terminal can be queried or changed using
these _OSC_ sequences introduced by **[rxvt-unicode](http://software.schmorp.de/pkg/rxvt-unicode.html)**:

| **sequence**         | **locale**       |
|:---------------------|:-----------------|
| `^[]701;?^G`         | query            |
| `^[]701;`_loc_`^G`   | set to _loc_     |
| `^[]701;^G`          | set to default   |

The locale string used here should take the same format as in the locale environment variables such as _LANG_. When the locale is queried, a sequence that would set the current locale is sent, e.g. `^[]701;C.UTF-8^G`. An empty _loc_ string selects the locale configured in the options or the environment.

Note: While the terminal character set defines how the terminal interprets 
and handles keys and characters, application handling of characters is 
usually determined by the locale environment, and they cannot automatically 
be tied to each other. If they do not match, character handling will be chaotic.
Consistent changing could be achieved with a shell script like 
**[changecs](change-charset.sh)**, 
to be declared in your shell profile (e.g. `$HOME/.bashrc`).


## Window title copy ##

The following _OSC_ ("operating system command") sequence can be used to copy 
the window title to the Windows clipboard (like menu function "Copy Title"):
This sequence is disabled by default setting `AllowSetSelection=no`.

> `^[]7721^G`


## Window title set ##

The following _OSC_ ("operating system command") sequence can be used to 
set the window title (alternatively to OSC 2):

> `^[]l;1^G`


## Window icon ##

The following _OSC_ ("operating system command") sequence can be used to 
set the window icon from the given file and optional icon index:

> `^[]I;icon_file,index^G`


## Working directory ##

The following _OSC_ ("operating system command") sequence can be used to 
inform mintty about the current working directory (as used in the Mac terminal), 
in order to spawn a new (cloned) terminal window in that directory 
(e.g. Alt+F2):

> `^[]7;`_file-URL_`^G`

The _file-URL_ liberally follows a `file:` URL scheme; examples are
  * `file:///home/tmp`
  * `//localhost/home/tmp`
  * `/home/tmp`
  * _(empty)_ to restore the default behaviour


## Hyperlinks ##

The following _OSC_ ("operating system command") sequence can be used to 
set a hyperlink attribute which is opened on Ctrl-click.

| **link control**         | **function**                                    |
|:-------------------------|:------------------------------------------------|
| `^[]8;;`_URL_`^G`        | underlay text with the hyperlink                |
| `^[]8;;^G`               | clear hyperlink attribute (terminate hyperlink) |
| `^[]8;id=`ID`;`_URL_`^G` | associate instances of hyperlink                |

A typical hyperlinked text would be written like
> `^[]8;;`_URL_`^G`text`^[]8;;^G`

Using the `id=` option, multiple parts of hyperlinked text can be 
associated to a single hyperlink, so a partially visible or wrapped 
hyperlinked text can be produced on the screen.
See [Hyperlinks (a.k.a. HTML-like anchors) in terminal emulators](https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda#viewers-editors)
for an example and a discussion.


## Scroll markers ##

The following sequence can be used to mark prompt lines in support of 
two features:
  * Shift+cursor-left/right navigates to the previous/next prompt line and scrolls in the scrollback buffer accordingly
  * user-defined commands can refer to environment variable MINED_OUTPUT which contains terminal output as limited by previous marker

| **marker**    | **function**                                              |
|:--------------|:----------------------------------------------------------|
| `^[[?7711h`   | mark prompt line (last line in case of multi-line prompt) |
| `^[[?7711l`   | mark secondary prompt line (upper lines)                  |


## Synchronous update ##

A pair of Begin/End Synchronous Update DECSET or DCS sequences suspends 
the output between them in order to be updated to the screen synchronously.
The purpose is that applications can control atomic screen update, 
in order to avoid screen flickering in certain situations of display update.

| **sequence**      | **function**                                        |
|:------------------|:----------------------------------------------------|
| `^[[?2026h`       | suspend screen update for 150 ms                    |
| `^[P=1s^[\`       | suspend screen update for 150 ms                    |
| `^[P=1;`_N_`s^[\` | suspend screen update for _N_ ms, max 420 ms        |
| `^[[?2026l`       | update screen (flush output), end update suspending |
| `^[P=2s^[\`       | update screen (flush output), end update suspending |


## Image support ##

In addition to the legacy Sixel feature, mintty supports graphic image display 
via iTerm2 controls:

> `^[]1337;File=` _par_`=`_arg_ [ `;`_par_`=`_arg_ ]* `:`_image_ `^G`

| **par**                  | **arg**            | **comment**               |
|:-------------------------|:-------------------|:--------------------------|
| **name=**                | base64-encoded ID  | currently not used        |
| **width=**               | size (*)           | cell/pixel/percentage     |
| **height=**              | size (*)           | cell/pixel/percentage     |
| **preserveAspectRatio=** | 1 _(default) or_ 0 | only used if **width** and **height** are given |
| _image_                  |                    | base64-encoded image data |

The width or height size arguments use cell units by default. Optionally, 
an appended "px" or "%" refers to the number of pixels or the percentage of 
screen size at the time of image output. Image size persists when resizing 
the terminal but scales when zooming the cell size.

If both width and height are given, the preserveAspectRatio parameter can 
select whether to fit the image in the denoted area or stretch it to fill it.
If only one of width or height are given, the other dimension is scaled so 
that the aspect ratio is preserved.
If none of width or height are given, the image pixel size is used.

Image formats supported comprise PNG, JPEG, GIF, TIFF, BMP, Exif.


## Graphics position ##

Sixel output is anchored at the current cursor position by default 
(Sixel scrolling mode). DECSET 80 (DECSDM) enables Sixel display mode instead, 
so Sixel output would start at the top of screen. Note this setting 
was reversed from mintty 3.0.1 to mintty 3.5.1, following xterm interpretation 
which has meanwhile also been fixed.
(The DECSDM setting can be disabled with option `SuppressDEC=80`.)

After output of a Sixel image in Sixel scrolling mode, or other image, 
the final cursor position can be next to the right bottom of the image, 
below the left bottom of the image (default), or at the line beginning 
below the image (like xterm). The mintty private sequence 7730 chooses 
between the latter two options and is overridden by the xterm 
control sequence 8452.

| **sequence**  | **exit position**    |
|:--------------|:---------------------|
| `^[[?7730h`   | line beginning below |
| `^[[?7730l`   | below left bottom    |
| `^[[?8452h`   | next to right bottom |
| `^[[?8452l`   | below image          |


## Audio support ##

— EXPERIMENTAL —

Mintty supports audio sound output with this OSC sequence:

> `^[]440;` _sound_ [ `:` _option_ ]* `^G`

where _sound_ is the name of a sound file (.wav) in a mintty configuration 
subdirectory _sounds_, or a path name of a .wav file.

| **option** | **comment**                                              |
|:-----------|:---------------------------------------------------------|
| **async**  | sound playing is decoupled from control processing       |
| **nostop** | sound is not played if an async sound is already playing |
| **loop**   | sound is played in asynchronous endless loop             |

An asynchronous sound can be stopped with an empty sound name:

> `^[]440;^G`

Mintty also supports the DECPS Play Sound escape sequence with 
tone style extension.

| **sequence**                                       | **function**         |
|:---------------------------------------------------|:---------------------|
| `^[[`_vol_`;`_duration_`;`_note_[`;`_note_...]`,~` | play notes (16 supported) |
| `^[[`_vol_`:`_tone_`;`_duration_`;`_note_[`;`_note_...]`,~` | set tone style and play |
| `^[[0:`_tone_`,~` | change tone style only |

The sequence plays the list of notes with the given volume (0...7) 
and duration (0...255) in units of 1/32 of a second.
Supported note values are 1...25, indicating notes c’’ (C5) to c’’’’ (C7).

The range of notes is mirrored to values 101...125 and extended to 41...137, 
indicating notes ,,C (C0) to c’’’’’ (C8). This extension is experimental 
and subject to withdrawal in later versions should other standard behaviour 
be established in terminals.

If the first parameter has a colon-separated sub-parameter appended, 
it sets the current tone style (which will persist until changed again 
or reset).
Tones 1 to 5 are currently defined, 1 is a sine waveform.

The initial tone style can be preselected by setting `PlayTone`; 
if changed by a sequence, it will be restored by a terminal reset.
Tone value 0 (also the fallback) resorts to the Windows Beep function.
Other tones are only effective if the audio output library `libao` 
is installed.


## Cursor style ##

The VT510 _[DECSCUSR](http://vt100.net/docs/vt510-rm/DECSCUSR)_ sequence 
can be used to control cursor type (shape) and blinking.
It takes an optional second parameter (proprietary extension) to set the 
blinking interval in milliseconds.

> `^[[` _arg_ _SP_ `q`

> `^[[` _arg_ `;` _blink_ _SP_ `q`

| **arg** | **shape**    | **blink** |
|:--------|:-------------|:----------|
| **0**   | default      | default   |
| **1**   | block        | yes       |
| **2**   | block        | no        |
| **3**   | underscore   | yes       |
| **4**   | underscore   | no        |
| **5**   | line         | yes       |
| **6**   | line         | no        |
| **7**   | box          | yes       |
| **8**   | box          | no        |

Furthermore, the following Linux console sequence can be used to set the 
size of the active underscore cursor.
(Note that the second and third parameters from the Linux sequence are not 
supported; cursor colour can be set with the OSC 12 sequence.)
The sequence also affects the vertical line cursor.

> `^[[?` _arg_ `c`

| **arg** | **size**     |
|:--------|:-------------|
| **0**   | default      |
| **1**   | invisible    |
| **2**   | underscore   |
| **3**   | lower_third  |
| **4**   | lower_half   |
| **5**   | two_thirds   |
| **6**   | full block   |


## Mouse pointer style ##

The following _OSC_ ("operating system command") sequence (xterm 367) 
can be used to set the mouse pointer shape of the current mouse mode 
(mintty maintains two different mouse pointer shapes, to distinguish 
application mouse reporting modes).
Valid values are Windows predefined cursor names 
(appstarting, arrow, cross, hand, help, ibeam, icon, no, size, sizeall, sizenesw, sizens, sizenwse, sizewe, uparrow, wait).
or cursor file names which are looked up in subdirectory `pointers` of 
a mintty resource directory; supported file types are .cur, .ico, .ani.

| **sequence**          |
|:----------------------|
| `^[]22;`_pointer_`^G` |


## ANSI colours ##

The following _OSC_ sequences can be used to set or query the foreground and
background variants of the ANSI colours.

| **sequence**                        | ** effect **                         |
|:------------------------------------|:-------------------------------------|
| `^[]7704;`_index_`;`_colour_`^G`    | set fg and bg variants to same value |
| `^[]7704;`_index_`;`_fg_`;`_bg_`^G` | set fg and bg to separate values     |
| `^[]7704;`_index_`;?^G`             | query current values                 |

The _index_ argument has to be in range 0 to 15.

The colour values can be comma-separated decimal triples such as `255,85,0`,
X11 colour names, or hexadecimal colour specifications such as `#`_RRGGBB_,
`rgb:`_RR_`/`_GG_`/`_BB_, `rgb:`_RRRR_`/`_GGGG_`/`_BBBB_,
`cmy:`_C_`.`_C_`/`_M_`.`_M_`/`_Y_`.`_Y_ or
`cmyk:`_C_`.`_C_`/`_M_`.`_M_`/`_Y_`.`_Y_`/`_K_`.`_K_.

If a colour value is left empty, it is reset to the value in the mintty
configuration. Invalid values are ignored.

The query sequence replies with the single-value sequence if the current values
for the foreground and background variants are the same, and with the two-value
sequence otherwise.

Note: Unlike the xterm-compatible sequence OSC 4, which sets palette colours 
including ANSI colours, OSC 7704 can be used to change ANSI colours only, 
leaving the associated palette colours 0..15 unchanged, so you could 
select different colours with SGR 30..37 etc distinct from SGR 38:5 etc.


## Printing and screen dump ##

Mintty supports the following DEC, xterm and mintty Media Copy sequences:

| **sequence** | **effect**                      |
|:-------------|:--------------------------------|
| `^[[0i`      | print screen to default printer |
| `^[[5i`      | redirect output to printer      |
| `^[[4i`      | end output to printer           |
| `^[[?5i`     | copy output to printer          |
| `^[[?4i`     | end output to printer           |
| `^[[10i`     | save screen as HTML             |
| `^[[12i`     | save screen as PNG image        |

