#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * Random platform-specific functions and constants for Windows.
 */

// Codepages.
static const int unicode_codepage = 65001; //CP_UTF8;
static const int ansi_codepage = 0; //CP_ACP;

// Clipboard data has to be NUL-terminated.
static const bool sel_nul_terminated = true;

// Copying to the clipboard terminates lines with CRLF.
static const wchar sel_nl[] = { '\r', '\n' };

// Clock ticks.
int get_tick_count(void);
int cursor_blink_ticks(void);

#endif
