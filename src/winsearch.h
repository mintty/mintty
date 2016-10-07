#ifndef TERMSEARCH_H
#define TERMSEARCH_H

#include "term.h"
#include "winpriv.h"
#include "termpriv.h"

#include <commctrl.h>

extern int SEARCHBAR_HEIGHT;

extern bool win_search_visible(void);
extern void win_open_search(void);
extern void win_update_search(void);
extern void win_paint_exclude_search(HDC dc);

#endif
