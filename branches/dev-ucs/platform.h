#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * Random platform-specific functions and constants for Windows.
 */

// Colours

typedef uint32 colour;

static inline colour
make_colour(uint8 r, uint8 g, uint8 b) { return r | g << 8 | b << 16; }

static inline uint8 red(colour c) { return c & 0xff; }
static inline uint8 green(colour c) { return c >> 8 & 0xff; }
static inline uint8 blue(colour c) { return c >> 16 & 0xff; }


// Codepages.
static const uint unicode_codepage = 65001; //CP_UTF8;
static const uint ansi_codepage = 0; //CP_ACP;

// Clipboard data has to be NUL-terminated.
static const bool sel_nul_terminated = true;

// Copying to the clipboard terminates lines with CRLF.
static const wchar sel_nl[] = { '\r', '\n' };

// Clock ticks.
int get_tick_count(void);
int cursor_blink_ticks(void);

#endif
