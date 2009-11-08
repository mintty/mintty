#ifndef WINCTRLS_H
#define WINCTRLS_H

#include "ctrls.h"
#include "tree234.h"

extern HINSTANCE inst;

/*
 * Exports from winctrls.c.
 */
typedef struct {
  HWND wnd;
  WPARAM font;
  int dlu4inpix;
  int ypos, width;
  int xoff;
  int boxystart, boxid;
  char *boxtext;
} ctrlpos;

/*
 * Exports from winctrls.c.
 */

/*
 * Private structure for prefslist state. Only in the header file
 * so that we can delegate allocation to callers.
 */
void ctrlposinit(ctrlpos * cp, HWND wnd, int leftborder, int rightborder,
                int topborder);

#define MAX_SHORTCUTS_PER_CTRL 16

/*
 * This structure is what's stored for each `union control' in the
 * portable-dialog interface.
 */
typedef struct {
  control *ctrl;
 /*
  * The control may have several components at the Windows
  * level, with different dialog IDs. To avoid needing N
  * separate platformsidectrl structures (which could be stored
  * separately in a tree234 so that lookup by ID worked), we
  * impose the constraint that those IDs must be in a contiguous
  * block.
  */
  int base_id;
  int num_ids;
 /*
  * Remember what keyboard shortcuts were used by this control,
  * so that when we remove it again we can take them out of the
  * list in the dlgparam.
  */
  char shortcuts[MAX_SHORTCUTS_PER_CTRL];
 /*
  * Some controls need a piece of allocated memory in which to
  * store temporary data about the control.
  */
  void *data;
} winctrl;

/*
 * And this structure holds a set of the above, in two separate
 * tree234s so that it can find an item by `union control' or by
 * dialog ID.
 */
typedef struct {
  tree234 *byctrl, *byid;
} winctrls;

/*
 * This describes a dialog box.
 */
typedef struct {
  HWND wnd;    /* the wnd of the dialog box */
  winctrls *controltrees[8];    /* can have several of these */
  int nctrltrees;
  char *wintitle;       /* title of actual window */
  void *data;   /* data to pass in refresh events */
  control *focused, *lastfocused; /* which ctrl has focus now/before */
  char shortcuts[128];  /* track which shortcuts in use */
  int coloursel_wanted; /* has an event handler asked for
                         * a colour selector? */
  colour coloursel_result;  /* 0-255 */
  bool coloursel_ok;
  tree234 *privdata;    /* stores per-control private data */
  int ended;            /* has the dialog been ended? */
} windlg;

extern windlg dlg;

void windlg_init(void);
void windlg_add_tree(winctrls *);
void windlg_cleanup(void);

void winctrl_init(winctrls *);
void winctrl_cleanup(winctrls *);
void winctrl_add(winctrls *, winctrl *);
void winctrl_remove(winctrls *, winctrl *);
winctrl *winctrl_findbyctrl(winctrls *, control *);
winctrl *winctrl_findbyid(winctrls *, int);
winctrl *winctrl_findbyindex(winctrls *, int);
void winctrl_layout(winctrls *, ctrlpos *, controlset *, int *id);
int winctrl_handle_command(UINT msg, WPARAM wParam, LPARAM lParam);
void winctrl_rem_shortcuts(winctrl *);

#endif
