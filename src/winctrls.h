#ifndef WINCTRLS_H
#define WINCTRLS_H

#include "ctrls.h"

extern HINSTANCE inst;

typedef struct {
  HWND wnd;
  WPARAM font;
  int dlu4inpix;
  int ypos, width;
  int xoff;
  int boxystart, boxid;
  char *boxtext;
} ctrlpos;


extern int scale_dialog(int x);
extern WPARAM diafont(void);

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
typedef struct winctrl {
  control *ctrl;
 /*
  * The control may have several components at the Windows
  * level, with different dialog IDs. We impose the constraint that
  * those IDs must be in a contiguous block.
  */
  int base_id;
  int num_ids;
 /*
  * Some controls need a piece of allocated memory in which to
  * store temporary data about the control.
  */
  void *data;
  struct winctrl *next;
} winctrl;

/*
 * And this structure holds a set of the above
 */
typedef struct {
  winctrl *first, *last;
} winctrls;

/*
 * This describes a dialog box.
 */
typedef struct {
  HWND wnd;    /* the wnd of the dialog box */
  winctrls *controltrees[8];    /* can have several of these */
  int nctrltrees;
  control *focused; /* which ctrl has focus now/before */
  int coloursel_wanted; /* has an event handler asked for
                         * a colour selector? */
  colour coloursel_result;  /* 0-255 */
  bool coloursel_ok;
  int ended;            /* has the dialog been ended? */
} windlg;

extern windlg dlg;

void windlg_init(void);
void windlg_add_tree(winctrls *);

void winctrl_init(winctrls *);
void winctrl_cleanup(winctrls *);
void winctrl_layout(winctrls *, ctrlpos *, controlset *, int *id);
int winctrl_handle_command(UINT msg, WPARAM wParam, LPARAM lParam);

#endif
