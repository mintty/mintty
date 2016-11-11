

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


## Scrollbar hiding ##

These sequences can be used to hide or show the scrollbar, whereby the window size remains the same but the number of character columns is changed to account for the width of the scrollbar. If the scrollbar is disabled in the options, it will always remain hidden.

| **sequence**  | **scrollbar** |
|:--------------|:--------------|
| `^[[?7766l`   | hide          |
| `^[[?7766h`   | show          |


## Shortcut override mode ##

When shortcut override mode is on, all shortcut key combinations are sent to the application instead of triggering window commands.

| **sequence**  | **override** |
|:--------------|:-------------|
| `^[[?7783l`   | off          |
| `^[[?7783h`   | on           |


## Mousewheel reporting ##

Mintty includes support for sending mousewheel events to an application without having to enable full [xterm mouse tracking](http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#Mouse%20Tracking), which takes over all mouse events and isn't supported by every application.

Mousewheel reporting only happens on the alternate screen, whereas on the primary screen, the mousewheel scrolls the scrollback buffer. The following two sequences enable or disable mousewheel reporting. It is enabled by default.

| **sequence**  | **reporting** |
|:--------------|:--------------|
| `^[[?7786l`   | disabled      |
| `^[[?7786h`   | enabled       |

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
Indic wide display in its forthcoming release).

Setting wide long Unicode characters mode, a number of Unicode characters 
that are supposed to be "wide" or "long" will be displayed 
in double-width (like CJK characters).
See comment above about cooperating applications.
The list of long Unicode characters considered "wide" is 
U+2001, U+2003, U+2014, U+27DD..U+27DE, U+27F5..U+27FF, 
U+2910, U+296A..U+296D, U+2B33, U+2E0E..U+2E11, U+2E3A..U+2E3B;
this list is subject to change in future versions.


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
When the font size is queried, a sequence that would restore the current size is sent, terminated with _ST_: `^[]7770;`_num_`^[\`.


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
When the font size is queried, a sequence that would restore the current size is sent, terminated with _ST_: `^[]7777;`_num_`^[\`.


## Locale ##

The locale and charset used by the terminal can be queried or changed using
these _OSC_ sequences introduced by **[rxvt-unicode](http://software.schmorp.de/pkg/rxvt-unicode.html)**:

| **sequence**         | **locale**       |
|:---------------------|:-----------------|
| `^[]701;?^G`         | query            |
| `^[]701;`_loc_`^G`   | set to _loc_     |
| `^[]701;^G`          | set to default   |

The locale string used here should take the same format as in the locale environment variables such as _LANG_. When the locale is queried, a sequence that would set the current locale is sent, e.g. `^[]701;C.UTF-8^G`. An empty _loc_ string selects the locale configured in the options or the environment.


## Window title ##

The following _OSC_ ("operating system command") sequence can be used to copy 
the window title to the Windows clipboard (like menu function "Copy Title"):

> `^[]7721;1^G`


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


## Sixel graphics end position ##

After output of a sixel image in sixel scrolling mode, 
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


## Cursor style ##

The VT510 _[DECSCUSR](http://vt100.net/docs/vt510-rm/DECSCUSR)_ sequence can be used to control cursor shape and blinking.

> `^[ [` _arg_ _SP_ `q`

| **arg** | **shape**    | **blink** |
|:--------|:-------------|:----------|
| **0**   | default      | default   |
| **1**   | block        | yes       |
| **2**   | block        | no        |
| **3**   | underscore   | yes       |
| **4**   | underscore   | no        |
| **5**   | line         | yes       |
| **6**   | line         | no        |