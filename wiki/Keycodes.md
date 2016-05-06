## Keyboard layout ##

The Windows keyboard layout is used to translate alphanumeric and symbol key 
presses into characters, which means that the keyboard layout can be 
switched using the standard Windows mechanisms for that purpose. 
[AltGr](http://en.wikipedia.org/wiki/AltGr_key) combinations, 
[dead keys](http://en.wikipedia.org/wiki/Dead_key), and 
[input method editors](http://en.wikipedia.org/wiki/Input_method_editor) (IMEs) 
are all supported.

Windows support for dead key combinations is limited to ANSI character ranges;
mintty extends that to Unicode by supporting combinations like ẃ.

Should the available keyboard layouts lack required features, Microsoft's 
**[Keyboard Layout Creator](http://www.microsoft.com/Globaldev/tools/msklc.mspx)** 
can be used to create custom keyboard layouts.

Mintty does not implement an 
X11-style [compose key](http://en.wikipedia.org/wiki/Compose_key); 
a seamless and stable tool to emulate a **Compose Key for Windows** is 
**[WinCompose](https://github.com/SamHocevar/wincompose)**.

For other keys and key combinations, mintty sends 
[xterm keycodes](http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#PC-Style%20Function%20Keys), 
with a few minor changes and some additions. 
Below, [Caret notation](http://en.wikipedia.org/wiki/Caret_notation) 
is used to show control characters.


## Alt and Meta ##

As is customary with PC keyboards, the [Alt](http://en.wikipedia.org/wiki/Alt_key) key acts as the so-called [Meta](http://en.wikipedia.org/wiki/Meta_key) modifier. When it is held down while pressing a key or key combination, the keycode is prefixed with an escape character, unless noted otherwise in the keycode tables in the following sections.

Encoding the meta modifier by setting the top bit of a character instead of prefixing it with the escape character is not supported, because that does not work for character codes beyond 7-bit [ASCII](http://en.wikipedia.org/wiki/ASCII).


## AltGr ##

The right Alt key, which is labelled [AltGr](http://en.wikipedia.org/wiki/AltGr_key) on most non-US keyboards, allows to type additional characters on many keyboard layouts. When the keyboard layout does not have a keycode for an AltGr combination, the AltGr key is treated as Alt instead. The _Ctrl+LeftAlt is AltGr_ setting allows combinations of either Ctrl key with the left Alt key to also be treated as AltGr.


## Ctrl ##

For key combinations involving [Ctrl](http://en.wikipedia.org/wiki/Control_key), the key combination's character code without the Ctrl is looked up in the Windows keyboard layout (whereby AltGr or Shift may be involved). If the resulting character corresponds to a control character, the control character will be sent to the application. For example, _Ctrl+]_ sends `^]`.

If the keyboard layout does not yield a character from the table below, the key's "[virtual keycode](http://msdn.microsoft.com/en-us/library/ms927178.aspx)" is tried instead, which usually corresponds to the US keyboard layout. This allows control characters to be entered when using a non-Latin keyboard layout.

If Shift is held in addition to a control character combination, the corresponding character from the so-called [C1 control](http://en.wikipedia.org/wiki/C1_controls) character set is sent (unless the resulting combination corresponds to a different control character in the first place). If the control character combination already contained a Shift, the second Shift key needs to be pressed to achieve this.

The C1 control characters are shown as Unicode codepoints in the table below. How exactly C1 control characters are sent depends on the selected character encoding. In [ISO-8859](http://en.wikipedia.org/wiki/ISO/IEC_8859) encodings, they are encoded as single bytes, e.g. `\x81` for _U+0081_. With [UTF-8](http://en.wikipedia.org/wiki/UTF-8), they are encoded as two-byte sequences, which effectively means that the character code is prefixed with a `\xC2` byte, so for example _U+0081_ becomes `\xC2\x81`. C1 codepoints that are not supported by the selected character encoding are sent by prefixing the corresponding ASCII control character with an _ESC_ character, so _U+0081_ would be sent as `^[^A`.

| **Char** | **Ctrl** | **Ctrl+Shift** |
|:---------|:---------|:---------------|
| **@**    | `^@`     | _U+0080_       |
| **a**    | `^A`     | _U+0081_       |
| **b**    | `^B`     | _U+0082_       |
| ...      |
| **y**    | `^Y`     | _U+0099_       |
| **z**    | `^Z`     | _U+009A_       |
| **[**    | `^[`     | _U+009B_       |
| **\**    | `^\`     | _U+009C_       |
| **]**    | `^]`     | _U+009D_       |
| **^**    | `^^`     | _U+009E_       |
| **`_`**  | `^_`     | _U+009F_       |
| **/**    | `^_`     | _U+009F_       |
| **?**    | `^?`     |                |


## Special keys ##

The keys here send the usual control characters, but there are a few mintty-specific additions that make combinations with modifier keys available as separate keycodes.

| **Key**    | **plain** | **Shift**  | **Ctrl**    | **Ctrl+Shift** |
|:-----------|:----------|:-----------|:------------|:---------------|
| **Tab**    | `^I`      | `^[[Z`     | `^[[1;5I`   | `^[[1;6I`      |
| **Space**  | _SP_      | _SP_       | `^@`        | _U+0080_       |
| **Enter**  | `^M`      | `^J`       | `^^`        | _U+009E_       |
| **Back**   | `^?`      | `^?`       | `^_`        | _U+009F_       |
| **Escape** | `^[`      | _U+009B_   |
| **Break**  | `^\`      | _U+009C_   |
| **Pause**  | `^]`      | _U+009D_   |

[Pause and Break](http://en.wikipedia.org/wiki/Pause_key) usually share a key, whereby Ctrl has to be pressed to get the Break function.

See the [previous section](#Ctrl.md) for how Unicode codepoints such as _U+009B_ are encoded.

## Modifier key encodings ##

Where the modifier keys Shift, Alt and Ctrl are not handled as described in the sections above, they are encoded as a one-digit number that becomes part of the keycode. To obtain that number, add the numbers for each pressed modifier to 1:

  * **Shift**: 1
  * **Alt**: 2
  * **Ctrl**: 4

For example, Shift+Ctrl would be encoded as the number 6 (1 plus 1 for Shift plus 4 for Ctrl). Modifiers are not double-counted if, for example, both Shift keys are pressed. In the following sections, modifier codes are shown as _m_.


## Number and symbol keys ##

Number and symbol key combinations that are not handled either by the Windows keyboard layout or by the Ctrl key processing described above, are assigned the keycodes shown here.

| **Key** | **modified**    | ** appl keypad modified** |
|:--------|:----------------|:--------------------------|
| **/**   | `^[[1;`_m_`o`   | `^[O`_m_`o`               |
| **`*`** | `^[[1;`_m_`j`   | `^[O`_m_`j`               |
| **-**   | `^[[1;`_m_`m`   | `^[O`_m_`m`               |
| **+**   | `^[[1;`_m_`k`   | `^[O`_m_`k`               |
| **Enter** |               | `^[O`_m_`M`               |
| **,**   | `^[[1;`_m_`l`   |
|         |                 | ** VT220 appl keypad modified** |
| **.**   | `^[[1;`_m_`n`   | `^[O`_m_`n`               |
| **0**   | `^[[1;`_m_`p`   | `^[O`_m_`p`               |
| **1**   | `^[[1;`_m_`q`   | `^[O`_m_`q`               |
| ...     |
| **8**   | `^[[1;`_m_`x`   | `^[O`_m_`x`               |
| **9**   | `^[[1;`_m_`y`   | `^[O`_m_`y`               |

(These are VT220 application keypad codes with added modifier.)


## Cursor keys ##

Cursor keycodes without modifier keys depend on whether "application cursor key mode" (controlled by the [DECCKM](http://vt100.net/docs/vt510-rm/DECCKM) sequence) is enabled. Application cursor mode is ignored if any modifier keys are down, and the modifier code is inserted into the keycode as shown.

By default, the [Home](http://en.wikipedia.org/wiki/Home_key) and [End](http://en.wikipedia.org/wiki/End_key) keys are considered cursor keys. If VT220 keyboard mode is enabled, they are treated as [editing keys](Keycodes#Editing_keys.md) instead. See the next section for their keycodes in that case.

| **Key**   | **plain** | **app**  | **modified**    |
|:----------|:----------|:---------|:----------------|
| **Up**    | `^[[A`    | `^[OA`   | `^[[1;`_m_`A`   |
| **Down**  | `^[[B`    | `^[OB`   | `^[[1;`_m_`B`   |
| **Left**  | `^[[D`    | `^[OD`   | `^[[1;`_m_`D`   |
| **Right** | `^[[C`    | `^[OC`   | `^[[1;`_m_`C`   |
| **Home**  | `^[[H`    | `^[OH`   | `^[[1;`_m_`H`   |
| **End**   | `^[[F`    | `^[OF`   | `^[[1;`_m_`F`   |


## Editing keys ##

There is no special application mode for the keys in the block that's usually above the arrow keys.

| **Key**   | **plain** | **modified**    |
|:----------|:----------|:----------------|
| **Ins**   | `^[[2~`   | `^[[2;`_m_`~`   |
| **Del**   | `^[[3~`   | `^[[3;`_m_`~`   |
| **PgUp**  | `^[[5~`   | `^[[5;`_m_`~`   |
| **PgDn**  | `^[[6~`   | `^[[6;`_m_`~`   |
| **Home**  | `^[[1~`   | `^[[1;`_m_`~`   |
| **End**   | `^[[4~`   | `^[[4;`_m_`~`   |

As mentioned in the [cursor keys](Keycodes#Cursor_keys.md) section, the Home and End keycodes shown here are only used if VT220 keyboard mode is enabled.


## Function keys ##

F1 through F4 send numpad-style keycodes, because they emulate the four [PF keys](http://vt100.net/docs/vt100-ug/figure3-2.html) above the number pad on the VT100 terminal. The remaining function keys send codes that were introduced with the VT220 terminal.

| **Key** | **plain**  | **modified**     |
|:--------|:-----------|:-----------------|
| **F1**  | `^[OP`     | `^[[1;`_m_`P`    |
| **F2**  | `^[OQ`     | `^[[1;`_m_`Q`    |
| **F3**  | `^[OR`     | `^[[1;`_m_`R`    |
| **F4**  | `^[OS`     | `^[[1;`_m_`S`    |
| **F5**  | `^[[15~`   | `^[[15;`_m_`~`   |
| **F6**  | `^[[17~`   | `^[[17;`_m_`~`   |
| **F7**  | `^[[18~`   | `^[[18;`_m_`~`   |
| **F8**  | `^[[19~`   | `^[[19;`_m_`~`   |
| **F9**  | `^[[20~`   | `^[[20;`_m_`~`   |
| **F10** | `^[[21~`   | `^[[21;`_m_`~`   |
| **F11** | `^[[23~`   | `^[[23;`_m_`~`   |
| **F12** | `^[[24~`   | `^[[24;`_m_`~`   |
| **F13** | `^[[25~`   | `^[[25;`_m_`~`   |
| **F14** | `^[[26~`   | `^[[26;`_m_`~`   |
| **F15** | `^[[28~`   | `^[[28;`_m_`~`   |
| **F16** | `^[[29~`   | `^[[29;`_m_`~`   |
| **F17** | `^[[31~`   | `^[[31;`_m_`~`   |
| **F18** | `^[[32~`   | `^[[32;`_m_`~`   |
| **F19** | `^[[33~`   | `^[[33;`_m_`~`   |
| **F20** | `^[[34~`   | `^[[34;`_m_`~`   |

The numbers in the keycodes really are that irregular. Only twelve function keys are usually available on a PC keyboard, but in VT220 keyboard mode, the Ctrl modifier adds 10 to the function key number, so that for example Ctrl+F3 sends the F13 keycode.


## Mousewheel ##

In xterm [mouse tracking](http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#Mouse%20Tracking) modes, the mousewheel is treated is a pair of mouse buttons. However, the mousewheel can also be used for scrolling in applications such as **[less](http://www.greenwoodsoftware.com/less)** that do not support xterm mouse reporting but that do use the alternate screen. Under those circumstances, mousewheel events are encoded as cursor up/down and page up/down keys. See the sections on the [cursor keys](Keycodes#Cursor_keys.md) and [editing keys](Keycodes#Editing_keys.md) for details.

The number of line up/down events sent per mousewheel notch depends on the relevant Windows setting on the _Wheel_ tab of the _Mouse_ control panel. Page up/down codes can be sent by holding down _Shift_ while scrolling. The Windows wheel setting can also be set to always scroll by a whole screen at a time.