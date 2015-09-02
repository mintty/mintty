#ifndef TERMSEARCH_H
#define TERMSEARCH_H

#include "term.h"
#include "winpriv.h"
#include "termpriv.h"

#include <commctrl.h>

#define SEARCHBARCLASS "SearchBar"
#define SEARCHBAR_HEIGHT 26

bool search_initialised;
bool searched;
HWND search_wnd;
HWND search_close_wnd;
HWND search_prev_wnd;
HWND search_next_wnd;
HWND search_edit_wnd;
WNDPROC default_edit_proc;
HFONT search_font;

void win_toggle_search(bool show, bool focus);
void win_open_search(void);
void win_hide_search(void);
void win_update_search(void);
void win_paint_exclude_search(HDC dc);
bool win_search_visible();

#endif
