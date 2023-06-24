// winctrls.c (part of mintty)
// Copyright 2008-11 Andy Koppe, 2015-2016 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// (corresponds to putty:windows/winctrls.c)
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winctrls.h"

#include "winpriv.h"
#include "charset.h"  // wcscpy

#include "res.h"  // DIALOG_FONT, DIALOG_FONTSIZE

#define _RPCNDR_H
#define _WTYPES_H
#define _OLE2_H
#include <commdlg.h>
#ifdef darken_dialog_elements
#include <commctrl.h>  // Subclass
#endif


/*
 * winctrls.c: routines to self-manage the controls in a dialog box.
 */

#define GAPBETWEEN 3
#define GAPWITHIN 1
#define GAPXBOX 7
#define GAPYBOX 4
#define DLGWIDTH 168
#define STATICHEIGHT 8
#define TITLEHEIGHT 12
#define CHECKBOXHEIGHT 8
#define RADIOHEIGHT 8
#define EDITHEIGHT 12
#define LISTHEIGHT 11
#define LISTINCREMENT 8
#define COMBOHEIGHT 12
#define PUSHBTNHEIGHT 14
#define PROGBARHEIGHT 14


static HFONT _diafont = 0;
static int diafontsize = 0;

static void
calc_diafontsize(void)
{
  cfg.options_fontsize = cfg.options_fontsize ?: DIALOG_FONTSIZE;

  int fontsize = - MulDiv(cfg.options_fontsize, dpi, 72);
  if (fontsize != diafontsize && _diafont) {
    DeleteObject(_diafont);
    _diafont = 0;
  }
  diafontsize = fontsize;
}

int
scale_dialog(int x)
{
  calc_diafontsize();

  return MulDiv(x, cfg.options_fontsize, DIALOG_FONTSIZE);
  // scale by another * 1.2 for Comic Sans MS ...?
}

WPARAM
diafont(void)
{
  calc_diafontsize();

  if (!_diafont && (*cfg.options_font || cfg.options_fontsize != DIALOG_FONTSIZE))
    _diafont = CreateFontW(
                 diafontsize, 0, 0, 0,
                 400, false, false, false,
                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 0,
                 *cfg.options_font ? cfg.options_font : W(DIALOG_FONT)
               );
  return (WPARAM)_diafont;
}

void
ctrlposinit(ctrlpos * cp, HWND wnd, int leftborder, int rightborder,
            int topborder)
{
  RECT r, r2;
  cp->wnd = wnd;
  cp->font = diafont() ?: (WPARAM)SendMessage(wnd, WM_GETFONT, 0, 0);
  cp->ypos = topborder;  // don't scale_dialog this; would move OK buttons out
  GetClientRect(wnd, &r);
  r2.left = r2.top = 0;
  r2.right = 4;
  r2.bottom = 8;
  MapDialogRect(wnd, &r2);
  cp->dlu4inpix = r2.right;
  cp->width = (r.right * 4) / (r2.right) - 2 * GAPBETWEEN;
  // scale Options dialog items horizontally with custom font
  cp->xoff = scale_dialog(leftborder);
  cp->width -= cp->xoff + scale_dialog(rightborder);
}

#ifdef darken_dialog_elements
static LRESULT CALLBACK
ctl_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uid, DWORD_PTR data)
{
  (void)uid; (void)data;
  switch (msg) {
    //when WM_ERASEBKGND:
      // makes things worse (flickering, invisible items)
    //when 0x0090 or 0x00F1 or 0x00F4 or 0x0143 or 0x014B:
      // these also occur
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}
#endif

static HWND
doctl(control * ctrl, 
      ctrlpos * cp, RECT r, 
      char * class, int wstyle, int exstyle, 
      string text, int wid)
{
  HWND ctl;

 /*
  * Note nonstandard use of RECT. This is deliberate: by
  * transforming the width and height directly we arrange to
  * have all supposedly same-sized controls really same-sized.
  */
  r.left += cp->xoff;
  MapDialogRect(cp->wnd, &r);

  // scale Options dialog items vertical position and size with custom font
  r.top = scale_dialog(r.top);
  r.bottom = scale_dialog(r.bottom);
#if 0
  // don't scale Options dialog items horizontally, done elsewhere
  r.left = scale_dialog(cp->xoff) + scale_dialog(r.left - scale_dialog(cp->xoff));
  r.right = scale_dialog(r.right);
#endif

 /*
  * We can pass in cp->wnd == null, to indicate a dry run
  * without creating any actual controls.
  */
  if (cp->wnd) {
    // avoid changing text with SendMessageW(ctl, WM_SETTEXT, ...) 
    // as this produces large text artefacts
    if (nonascii(text)) {
      // transform label for proper Windows display
      wchar * wtext = text ? cs__utftowcs(text) : 0;
      wchar * wclass = class ? cs__utftowcs(class) : 0;
      ctl =
        CreateWindowExW(exstyle, wclass, wtext, wstyle,
                        r.left, r.top, r.right, r.bottom,
                        cp->wnd, (HMENU)(INT_PTR)wid, inst, null);
      if (wtext)
        free(wtext);
      if (wclass)
        free(wclass);
    }
    else {
      ctl =
        CreateWindowExA(exstyle, class, text, wstyle,
                        r.left, r.top, r.right, r.bottom,
                        cp->wnd, (HMENU)(INT_PTR)wid, inst, null);
    }
#ifdef darken_dialog_elements
    // apply dark mode to dialog buttons
    win_dark_mode(ctl);
    // try to darken further elements
    SetWindowSubclass(ctl, ctl_proc, 0, 0);
#endif
#ifdef debug_widgets
    printf("%8p %s %d '%s'\n", ctl, class, exstyle, text);
#endif
    SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(true, 0));
    if (ctrl) {
      ctrl->widget = ctl;
#ifdef debug_dragndrop
      printf("%d %s %8p\n", ctrl->type, ctrl->label, ctl);
#endif
    }

#ifdef register_sub_widgets
    // find magically created sub-widgets
    BOOL CALLBACK enumwin(HWND hwnd, LPARAM lParam)
    {
      (void)lParam;
      ctrl->subwidget = hwnd;  // for action (not needed for drag-and-drop)
      return FALSE;  // don't proceed; register first sub-widget only
    }
    EnumChildWindows(ctl, enumwin, 0);
#endif

    if (!strcmp(class, "LISTBOX")) {
     /*
      * Bizarre Windows bug: the list box calculates its
      * number of lines based on the font it has at creation
      * time, but sending it WM_SETFONT doesn't cause it to
      * recalculate. So now, _after_ we've sent it
      * WM_SETFONT, we explicitly resize it (to the same
      * size it was already!) to force it to reconsider.
      */
      SetWindowPos(ctl, null, 0, 0, r.right, r.bottom,
                   SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
    }
  }
  else
    ctl = null;
  return ctl;
}

/*
 * Begin a grouping box, with or without a group title.
 */
static void
beginbox(ctrlpos * cp, char *name, int idbox)
{
  cp->boxystart = cp->ypos;
  if (!name)
    cp->boxystart -= STATICHEIGHT / 2;
  if (name)
    cp->ypos += STATICHEIGHT;
  cp->ypos += GAPYBOX;
  cp->width -= 2 * GAPXBOX;
  cp->xoff += GAPXBOX;
  cp->boxid = idbox;
  cp->boxtext = name;
}

/*
 * End a grouping box.
 */
static void
endbox(ctrlpos * cp)
{
  RECT r;
  cp->xoff -= GAPXBOX;
  cp->width += 2 * GAPXBOX;
  cp->ypos += GAPYBOX - GAPBETWEEN;
  r.left = GAPBETWEEN;
  r.right = cp->width;
  r.top = cp->boxystart;
  r.bottom = cp->ypos - cp->boxystart;
  doctl(null, cp, r, "BUTTON", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, 0,
        cp->boxtext ? cp->boxtext : "", cp->boxid);
  cp->ypos += GAPYBOX;
}

/*
 * A static line, followed by a full-width edit box.
 */
static void
editbox(control * ctrl, ctrlpos * cp, int password, char * text, 
        int staticid, int editid)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.right = cp->width;

  if (text) {
    r.top = cp->ypos;
    r.bottom = STATICHEIGHT;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, staticid);
    cp->ypos += STATICHEIGHT + GAPWITHIN;
  }
  r.top = cp->ypos;
  r.bottom = EDITHEIGHT;
  doctl(ctrl, cp, r, "EDIT",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (password ?
                                                               ES_PASSWORD : 0),
        WS_EX_CLIENTEDGE, "", editid);
  cp->ypos += EDITHEIGHT + GAPBETWEEN;
}

/*
 * A static line, followed by a full-width combo box.
 */
static void
combobox(control * ctrl, ctrlpos * cp, char *text, int staticid, int listid)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.right = cp->width;

  if (text) {
    r.top = cp->ypos;
    r.bottom = STATICHEIGHT;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, staticid);
    cp->ypos += STATICHEIGHT + GAPWITHIN;
  }
  r.top = cp->ypos;
  r.bottom = COMBOHEIGHT * 10;
  doctl(ctrl, cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", listid);
  cp->ypos += COMBOHEIGHT + GAPBETWEEN;
}

typedef struct {
  string label;
  int id;
} radio;

static void
radioline_common(ctrlpos * cp, char *text, int id, int nacross,
                 radio * buttons, int nbuttons)
{
  RECT r;
  int group;
  int i;
  int j;

  if (text) {
    r.left = GAPBETWEEN;
    r.top = cp->ypos;
    r.right = cp->width;
    r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, id);
  }

  group = WS_GROUP;
  i = 0;
  for (j = 0; j < nbuttons; j++) {
    if (i == nacross) {
      cp->ypos += r.bottom + (nacross > 1 ? GAPBETWEEN : GAPWITHIN);
      i = 0;
    }
    r.left = GAPBETWEEN + i * (cp->width + GAPBETWEEN) / nacross;
    if (j < nbuttons - 1)
      r.right = (i + 1) * (cp->width + GAPBETWEEN) / nacross - r.left;
    else
      r.right = cp->width - r.left;
    r.top = cp->ypos;
    r.bottom = RADIOHEIGHT;
    doctl(null, cp, r, "BUTTON",
          BS_NOTIFY | BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          group, 0, buttons[j].label, buttons[j].id);
    group = 0;
    i++;
  }
  cp->ypos += r.bottom + GAPBETWEEN;
}

/*
 * A single standalone checkbox.
 */
static void
checkbox(ctrlpos * cp, char *text, int id)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = CHECKBOXHEIGHT;
  cp->ypos += r.bottom + GAPBETWEEN;
  doctl(null, cp, r, "BUTTON",
        BS_NOTIFY | BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
        text, id);
}

/*
 * An owner-drawn static text control for a panel title.
 */
static void
paneltitle(ctrlpos * cp, int id)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = TITLEHEIGHT;
  cp->ypos += r.bottom + GAPBETWEEN;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, null, id);
}

/*
 * A plain text.
 */
static void
statictext(ctrlpos * cp, char * stext, int sid)
{
  RECT r;

  cp->ypos -= GAPBETWEEN;

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = STATICHEIGHT;
  cp->ypos += r.bottom + GAPBETWEEN;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
}

/*
 * A button on the right hand side, with a static to its left.
 */
static void
staticbtn(ctrlpos * cp, char *stext, int sid, char *btext, int bid)
{
  const int height =
    (PUSHBTNHEIGHT > STATICHEIGHT ? PUSHBTNHEIGHT : STATICHEIGHT);
  RECT r;
  int lwid, rwid, rpos;

  rpos = GAPBETWEEN + 3 * (cp->width + GAPBETWEEN) / 4;
  lwid = rpos - 2 * GAPBETWEEN;
  rwid = cp->width + GAPBETWEEN - rpos;

  r.left = GAPBETWEEN;
  r.top = cp->ypos + (height - STATICHEIGHT) / 2 - 1;
  r.right = lwid;
  r.bottom = STATICHEIGHT;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - PUSHBTNHEIGHT) / 2 - 1;
  r.right = rwid;
  r.bottom = PUSHBTNHEIGHT;
  doctl(null, cp, r, "BUTTON",
        BS_NOTIFY | WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
        btext, bid);

  cp->ypos += height + GAPBETWEEN;
}

/*
 * A simple push button.
 */
static void
button(control * ctrl, ctrlpos * cp, char *btext, int bid, int defbtn)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.top = cp->ypos - 1;
  r.right = cp->width;
  r.bottom = PUSHBTNHEIGHT;

 /* Q67655: the _dialog box_ must know which button is default
  * as well as the button itself knowing */
  if (defbtn && cp->wnd)
    SendMessage(cp->wnd, DM_SETDEFID, bid, 0);

  //HWND but = // if we'd want to send it a message right away
  doctl(ctrl, cp, r, "BUTTON",
          BS_NOTIFY | WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          (defbtn ? BS_DEFPUSHBUTTON : 0) | BS_PUSHBUTTON, 0, btext, bid);
#ifdef need_to_disable_widgets_here
  // disabled prototype hack to disable a widget initially;
  // now achieved by enable_widget() in config.c
  if (!strcmp(btext, _("Store"))) {
    EnableWindow(but, FALSE);
  }
#endif

  cp->ypos += PUSHBTNHEIGHT + GAPBETWEEN;
}

/*
 * An edit control on the right hand side, with a static to its left.
 */
static void
staticedit_internal(ctrlpos * cp, char *stext, int sid, int eid,
                    int percentedit, int style)
{
  const int height = (EDITHEIGHT > STATICHEIGHT ? EDITHEIGHT : STATICHEIGHT);
  RECT r;
  int lwid, rwid, rpos;

  rpos = GAPBETWEEN + (100 - percentedit) * (cp->width + GAPBETWEEN) / 100;
  lwid = rpos - 2 * GAPBETWEEN;
  rwid = cp->width + GAPBETWEEN - rpos;

  r.left = GAPBETWEEN;
  r.top = cp->ypos + (height - STATICHEIGHT) / 2;
  r.right = lwid;
  r.bottom = STATICHEIGHT;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = EDITHEIGHT;
  doctl(null, cp, r, "EDIT",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | style,
        WS_EX_CLIENTEDGE, "", eid);

  cp->ypos += height + GAPBETWEEN;
}

static void
staticedit(ctrlpos * cp, char *stext, int sid, int eid, int percentedit)
{
  staticedit_internal(cp, stext, sid, eid, percentedit, 0);
}

static void
staticpassedit(ctrlpos * cp, char *stext, int sid, int eid, int percentedit)
{
  staticedit_internal(cp, stext, sid, eid, percentedit, ES_PASSWORD);
}

/*
 * A drop-down list box on the right hand side, with a static to its left.
 */
static void
staticddl(ctrlpos * cp, char *stext, int sid, int lid, int percentlist)
{
  const int height = (COMBOHEIGHT > STATICHEIGHT ? COMBOHEIGHT : STATICHEIGHT);
  RECT r;
  int lwid, rwid, rpos;

  rpos = GAPBETWEEN + (100 - percentlist) * (cp->width + GAPBETWEEN) / 100;
  lwid = rpos - 2 * GAPBETWEEN;
  rwid = cp->width + GAPBETWEEN - rpos;

  r.left = GAPBETWEEN;
  r.top = cp->ypos + (height - STATICHEIGHT) / 2;
  r.right = lwid;
  r.bottom = STATICHEIGHT;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = COMBOHEIGHT * 4;
  doctl(null, cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", lid);

  cp->ypos += height + GAPBETWEEN;
}

/*
 * A combo box on the right hand side, with a static to its left.
 */
static void
staticcombo(ctrlpos * cp, char *stext, int sid, int lid, int percentlist)
{
  const int height = (COMBOHEIGHT > STATICHEIGHT ? COMBOHEIGHT : STATICHEIGHT);
  RECT r;
  int lwid, rwid, rpos;

  rpos = GAPBETWEEN + (100 - percentlist) * (cp->width + GAPBETWEEN) / 100;
  lwid = rpos - 2 * GAPBETWEEN;
  rwid = cp->width + GAPBETWEEN - rpos;

  r.left = GAPBETWEEN;
  r.top = cp->ypos + (height - STATICHEIGHT) / 2;
  r.right = lwid;
  r.bottom = STATICHEIGHT;
  doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = COMBOHEIGHT * 10;
  doctl(null, cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", lid);

  cp->ypos += height + GAPBETWEEN;
}

/*
 * A static, with a full-width drop-down list box below it.
 */
static void
staticddlbig(ctrlpos * cp, char *stext, int sid, int lid)
{
  RECT r;

  if (stext) {
    r.left = GAPBETWEEN;
    r.top = cp->ypos;
    r.right = cp->width;
    r.bottom = STATICHEIGHT;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
    cp->ypos += STATICHEIGHT;
  }

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = COMBOHEIGHT * 4;
  doctl(null, cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", lid);
  cp->ypos += COMBOHEIGHT + GAPBETWEEN;
}

/*
 * A static, text/label only.
 */
static void
staticlabel(ctrlpos * cp, char *stext, int sid)
{
  RECT r;

  if (stext) {
    r.left = GAPBETWEEN;
    r.top = cp->ypos;
    r.right = cp->width;
    r.bottom = STATICHEIGHT;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
    cp->ypos += STATICHEIGHT + GAPBETWEEN;
  }
}

/*
 * A list box with a static labelling it.
 */
static void
listbox(control * ctrl, ctrlpos * cp, char *stext, int sid, int lid, int lines)
{
  RECT r;

  if (stext != null) {
    r.left = GAPBETWEEN;
    r.top = cp->ypos;
    r.right = cp->width;
    r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(null, cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
  }

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = LISTHEIGHT + (lines - 1) * LISTINCREMENT;
  cp->ypos += r.bottom + GAPBETWEEN;
  doctl(ctrl, cp, r, "LISTBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
        LBS_HASSTRINGS | LBS_USETABSTOPS, WS_EX_CLIENTEDGE, "", lid);
}

/* ----------------------------------------------------------------------
 * Platform-specific side of portable dialog-box mechanism.
 */

void
winctrl_init(winctrls *wc)
{
  wc->first = wc->last = null;
}

void
winctrl_cleanup(winctrls *wc)
{
  winctrl *c = wc->first;
  while (c) {
    winctrl *next = c->next;
    if (c->ctrl) {
      c->ctrl->plat_ctrl = null;
    }
    free(c->data);
    free(c);
    c = next;
  }
  wc->first = wc->last = null;
}

static void
winctrl_add(winctrls *wc, winctrl *c)
{
  if (wc->last)
    wc->last->next = c;
  else
    wc->first = c;
  wc->last = c;
  if (c->ctrl)
    c->ctrl->plat_ctrl = c;
}

static winctrl *
winctrl_findbyid(winctrls *wc, int id)
{
  for (winctrl *c = wc->first; c; c = c->next) {
    if (id >= c->base_id && id < c->base_id + c->num_ids)
      return c;
  }
  return 0;
}

static winctrl *
new_winctrl(int base_id, void *data)
{
  winctrl *c = new(winctrl);
  c->next = null;
  c->ctrl = null;
  c->base_id = base_id;
  c->num_ids = 1;
  c->data = data;
  return c;
}

void
winctrl_layout(winctrls *wc, ctrlpos *cp, controlset *s, int *id)
{
  ctrlpos columns[16];
  int ncols, colstart, colspan;

  ctrlpos pos;

  int actual_base_id, base_id, num_ids;
  void *data;

  base_id = *id;

#ifdef debug_layout
  printf("winctrl_layout cols %d nctl %d titl %s path %s\n", s->ncolumns, s->ncontrols, s->boxtitle, s->pathname);
#endif
  if (!s->ncolumns) {
   /* Draw a title of an Options dialog panel. */
    winctrl *c = new_winctrl(base_id, strdup(s->boxtitle));
    winctrl_add(wc, c);
    paneltitle(cp, base_id);
    base_id++;
  }
  else if (*s->pathname) {
   /* Start a containing box. */
    winctrl *c = new_winctrl(base_id, null);
    winctrl_add(wc, c);
    beginbox(cp, s->boxtitle, base_id);
    base_id++;
  }


 /* Initially we have just one column. */
  ncols = 1;
  columns[0] = *cp;     /* structure copy */

 /* Loop over each control in the controlset. */
  for (int i = 0; i < s->ncontrols; i++) {
    control *ctrl = s->ctrls[i];

   /*
    * Generic processing that pertains to all control types.
    * At the end of this if statement, we'll have produced
    * `ctrl' (a pointer to the control we have to create, or
    * think about creating, in this iteration of the loop),
    * `pos' (a suitable ctrlpos with which to position it), and
    * `c' (a winctrl structure to receive details of the dialog IDs).
    * Or we'll have done a `continue', if it was CTRL_COLUMNS 
    * and doesn't require any control creation at all.
    */
    if (ctrl->type == CTRL_COLUMNS) {
      assert((ctrl->columns.ncols == 1) ^ (ncols == 1));

      if (ncols == 1) {
       /*
        * We're splitting into multiple columns.
        */
        int lpercent, rpercent, lx, rx, i;

        ncols = ctrl->columns.ncols;
        assert(ncols <= (int) lengthof(columns));
        for (i = 1; i < ncols; i++)
          columns[i] = columns[0];      /* structure copy */

        lpercent = 0;
        for (i = 0; i < ncols; i++) {
          rpercent = lpercent + ctrl->columns.percentages[i];
          lx =
            columns[i].xoff + lpercent * (columns[i].width + GAPBETWEEN) / 100;
          rx =
            columns[i].xoff + rpercent * (columns[i].width + GAPBETWEEN) / 100;
          columns[i].xoff = lx;
          columns[i].width = rx - lx - GAPBETWEEN;
          lpercent = rpercent;
        }
      }
      else {
       /*
        * We're recombining the various columns into one.
        */
        int maxy = columns[0].ypos;
        int i;
        for (i = 1; i < ncols; i++)
          if (maxy < columns[i].ypos)
            maxy = columns[i].ypos;
        ncols = 1;
        columns[0] = *cp;       /* structure copy */
        columns[0].ypos = maxy;
      }

      continue;
    }
    else {
     /*
      * If it wasn't one of those, it's a genuine control;
      * so we'll have to compute a position for it now, by
      * checking its column span.
      */
      int col;

      colstart = COLUMN_START(ctrl->column);
      colspan = COLUMN_SPAN(ctrl->column);

      pos = columns[colstart];  /* structure copy */
      pos.width =
        columns[colstart + colspan - 1].width +
        (columns[colstart + colspan - 1].xoff - columns[colstart].xoff);

      for (col = colstart; col < colstart + colspan; col++)
        if (pos.ypos < columns[col].ypos)
          pos.ypos = columns[col].ypos;
    }

   /* Most controls don't need anything in c->data. */
    data = null;

   /* Almost all controls start at base_id. */
    actual_base_id = base_id;

   /*
    * Now we're ready to actually create the control, by
    * switching on its type.
    */
    switch (ctrl->type) {
      when CTRL_EDITBOX: {
        num_ids = 2;    /* static, edit */
        if (ctrl->editbox.percentwidth == 100) {
          if (ctrl->editbox.has_list)
            combobox(ctrl, &pos, ctrl->label, base_id, base_id + 1);
          else
            editbox(ctrl, &pos, ctrl->editbox.password, ctrl->label, 
                    base_id, base_id + 1);
        }
        else {
          if (ctrl->editbox.has_list) {
            staticcombo(&pos, ctrl->label, base_id, base_id + 1,
                        ctrl->editbox.percentwidth);
          }
          else {
            (ctrl->editbox.password ? staticpassedit : staticedit)
              (&pos, ctrl->label, base_id, base_id + 1, ctrl->editbox.percentwidth);
          }
        }
      }
      when CTRL_RADIO: {
        num_ids = ctrl->radio.nbuttons + 1;     /* label as well */
        radio buttons[ctrl->radio.nbuttons];

        for (int i = 0; i < ctrl->radio.nbuttons; i++) {
          buttons[i].label = ctrl->radio.labels[i];
          buttons[i].id = base_id + 1 + i;
        }

        radioline_common(&pos, ctrl->label, base_id, ctrl->radio.ncolumns,
                         buttons, ctrl->radio.nbuttons);
      }
      when CTRL_CHECKBOX: {
        num_ids = 1;
        checkbox(&pos, ctrl->label, base_id);
      }
      when CTRL_BUTTON: {
        if (ctrl->button.iscancel)
          actual_base_id = IDCANCEL;
        num_ids = 1;
        button(ctrl, &pos, ctrl->label, actual_base_id, ctrl->button.isdefault);
      }
      when CTRL_LISTBOX: {
        num_ids = 2;
        if (ctrl->listbox.height == 0) {
         /* Drop-down list. */
          if (ctrl->listbox.percentwidth == 100)
            staticddlbig(&pos, ctrl->label, base_id, base_id + 1);
          else
            staticddl(&pos, ctrl->label, base_id, base_id + 1,
                      ctrl->listbox.percentwidth);
        }
        else {
         /* Ordinary list. */
          listbox(ctrl, &pos, ctrl->label, base_id, base_id + 1, ctrl->listbox.height);
        }
        if (ctrl->listbox.ncols) {
         /*
          * This method of getting the box width is a bit of
          * a hack; we'd do better to try to retrieve the
          * actual width in dialog units from doctl() just
          * before MapDialogRect. But that's going to be no
          * fun, and this should be good enough accuracy.
          */
          int width = cp->width * ctrl->listbox.percentwidth;
          int tabarray[ctrl->listbox.ncols - 1];
          int percent = 0;
          for (int i = 0; i < ctrl->listbox.ncols - 1; i++) {
            percent += ctrl->listbox.percentages[i];
            tabarray[i] = width * percent / 10000;
          }
          SendDlgItemMessage(cp->wnd, base_id + 1, LB_SETTABSTOPS,
                             ctrl->listbox.ncols - 1, (LPARAM) tabarray);
        }
      }
      when CTRL_LABEL:
        num_ids = 1;
        staticlabel(&pos, ctrl->label, base_id);
      when CTRL_FONTSELECT: {
        num_ids = 3;
        //__ Options - Text: font chooser activation button
        staticbtn(&pos, "", base_id + 1, _("&Select..."), base_id + 2);
        data = new(font_spec);

        char * fontinfo = fontpropinfo();
        if (fontinfo) {
          num_ids++;
          statictext(&pos, fontinfo, base_id + 3);
          free(fontinfo);
        }
      }
      otherwise:
        assert(!"Can't happen");
        num_ids = 0;    /* placate gcc */
    }

   /*
    * Create a `winctrl' for this control, and advance
    * the dialog ID counter, if it's actually been created
    */
    if (pos.wnd) {
      winctrl *c = new(winctrl);
      c->next = null;
      c->ctrl = ctrl;
      c->base_id = actual_base_id;
      c->num_ids = num_ids;
      c->data = data;
      winctrl_add(wc, c);
      if (actual_base_id == base_id)
        base_id += num_ids;
    }

    if (colstart >= 0) {
     /*
      * Update the ypos in all columns crossed by this control.
      */
      int i;
      for (i = colstart; i < colstart + colspan; i++)
        columns[i].ypos = pos.ypos;
    }
  }

 /*
  * We've now finished laying out the controls; so now update
  * the ctrlpos and control ID that were passed in, terminate
  * any containing box, and return.
  */
  for (int i = 0; i < ncols; i++)
    if (cp->ypos < columns[i].ypos)
      cp->ypos = columns[i].ypos;
  *id = base_id;

  if (s->ncolumns && *s->pathname)
    endbox(cp);
}

static void
winctrl_set_focus(control *ctrl, int has_focus)
{
  if (has_focus)
    dlg.focused = ctrl;
  else if (!has_focus && dlg.focused == ctrl)
    dlg.focused = null;
}


/*
   adapted from messageboxmanager.zip
   @ https://www.codeproject.com/articles/18399/localizing-system-messagebox
 */
static HHOOK windows_hook = 0;
static bool hooked_window_activated = false;

static void
hook_windows(HOOKPROC hookproc)
{
  windows_hook = SetWindowsHookExW(WH_CBT, hookproc, 0, GetCurrentThreadId());
}

static void
unhook_windows()
{
  UnhookWindowsHookEx(windows_hook);
  hooked_window_activated = false;
}

static HWND font_sample = 0;

#define dont_debug_dialog_hook 1

//static bool do_next_hook = true;
static bool do_next_hook = false;  // ignore other hooks

#ifdef debug_dialog_hook
static FILE * dout = 0;

static bool
init_debug()
{
  if (!dout) {
    char * debugopt = getenv("MINTTY_DEBUG");
    if (debugopt && strchr(debugopt, 'x'))
      return false;
    if (debugopt && strchr(debugopt, 'h')) {
      chdir(getenv("TEMP"));
      dout = fopen("mintty.debug", "w");
      if (strchr(debugopt, 'n'))
        do_next_hook = false;
    }
  }
  if (!dout) {
    dout = stdout;
  }
  return true;
}

#define trace_hook(...)	fprintf(dout, __VA_ARGS__); fflush(dout)

#else
#define trace_hook(...)	
#endif

static LRESULT
set_labels(bool font_chooser, int nCode, WPARAM wParam, LPARAM lParam)
{
  bool localize = *cfg.lang;

#ifdef debug_dialog_hook
  if (!init_debug())
    return 0;

  void trace_label(int id, HWND button, wstring label)
  {
    if (!button) button = GetDlgItem((HWND)wParam, id);
    wchar buf [99];
    GetWindowTextW(button, buf, 99);
    trace_hook("%d [%8p] <%ls> -> <%ls>\n", id, button, buf, label);
  }

  char * hcbt[] = {
    "HCBT_MOVESIZE",
    "HCBT_MINMAX",
    "HCBT_QS",
    "HCBT_CREATEWND",
    "HCBT_DESTROYWND",
    "HCBT_ACTIVATE",
    "HCBT_CLICKSKIPPED",
    "HCBT_KEYSKIPPED",
    "HCBT_SYSCOMMAND",
    "HCBT_SETFOCUS",
  };
  char * sCode = "?";
  if (nCode >= 0 && nCode < (int)lengthof(hcbt))
    sCode = hcbt[nCode];
  trace_hook("hook %d %s", nCode, sCode);
  if (nCode == HCBT_CREATEWND) {
    CREATESTRUCTW * cs = ((CBT_CREATEWNDW *)lParam)->lpcs;
    trace_hook(" x %3d y %3d w %3d h %3d (%08X %07X) <%ls>\n", cs->x, cs->y, cs->cx, cs->cy, (uint)cs->style, (uint)cs->dwExStyle, cs->lpszName);
    //(long)cs->lpCreateParams == 0
  }
  else if (nCode == HCBT_ACTIVATE) {
    bool from_mouse = ((CBTACTIVATESTRUCT *)lParam)->fMouse;
    trace_hook(" mou %d\n", from_mouse);
  }
  else {
    trace_hook("\n");
  }
#endif

  void setfont(int id)
  {
    HWND button = GetDlgItem((HWND)wParam, id);
    if (diafont())
      SendMessage(button, WM_SETFONT, diafont(), MAKELPARAM(true, 0));
  }

  void setlabel(int id, wstring label)
  {
    HWND button = GetDlgItem((HWND)wParam, id);
    if (localize && button) {
#ifdef debug_dialog_hook
      trace_label(id, button, label);
#endif
      SetWindowTextW(button, label);
    }

    // adjust custom font
    setfont(id);
  }

  static int adjust_sample = 0;  /* pre-adjust (here) vs post-adjust (below)
                                    0b01: post-adjust "Sample" frame
                                    0b02: post-adjust sample text
                                 */
  static bool adjust_sample_plus = false;
  static int fc_item_left = 11;
  static int fc_item_right = 415;
  static int fc_sample_gap = 12;
  static int fc_sample_bottom = 228;

  if (nCode == HCBT_CREATEWND) {
    // dialog item geometry calculations and adjustments
    CREATESTRUCTW * cs = ((CBT_CREATEWNDW *)lParam)->lpcs;

    if (font_chooser && (new_cfg.fontmenu & 4)) {
      static int fc_item_grp = 0;
      static int fc_item_idx = 0;
      static int fc_item_gap = 7;
      static int fc_button_width = 68;
      static int fc_width = 437;
      static int fc_item1_right = 158;
      static int fc_item2_left = 165;
      static int fc_sample_left = 165;

      // we could also adjust the font chooser dialog window size here
      // but then the localization transformations below would not work anymore,
      // because SetWindowPos would cause HCBT_ACTIVATE to be invoked
      // when the dialog is not yet populated with the other dialog items

      // check for style property combinations to identify sets of dialog items;
      // count within group to identify actual font chooser dialog item;
      // note: we cannot identify by geometry which may differ between systems
      // 0x80000000L WS_POPUP
      // 0x40000000L WS_CHILD
      // 0x10000000L WS_VISIBLE
      // 0x00020000L WS_GROUP
      // 0x00010000L WS_TABSTOP
      if (!(cs->style & WS_CHILD)) {
        // font chooser popup dialog
        fc_item_grp = 0;
        WINDOWINFO winfo;
        winfo.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo((HWND)wParam, &winfo);
        fc_width = cs->cx - 2 * winfo.cxWindowBorders;
      }
      else if ((cs->style & WS_CHILD) && !(cs->style & WS_VISIBLE)) {
        // font sample text (default "AaBbYyZz")
        fc_sample_gap = cs->x - fc_sample_left;
        if (!(adjust_sample & 2)) {
          cs->x = fc_item_left + fc_sample_gap;
          //cs->cx = fc_item_right - fc_item_left - 2 * fc_sample_gap; // | Size
          cs->cx = fc_width - 2 * (fc_item_left + fc_sample_gap);
          if (adjust_sample_plus)
            cs->cx += fc_item_gap + fc_button_width;  // not yet known
        }
      }
      else if ((cs->style & WS_CHILD) && (cs->style & WS_GROUP) && (cs->style & WS_TABSTOP)) {
        // buttons (OK, Cancel, Apply)
        fc_button_width = cs->cx;
        fc_item_right = cs->x + cs->cx;  // only useful for post-adjustment
        (void)fc_item_right;
      }
      else if ((cs->style & WS_CHILD) && (cs->style & WS_GROUP)) {
        fc_item_grp ++;
        fc_item_idx = 0;
        if (fc_item_grp == 1) { // "Font:"
          fc_item_left = cs->x;
        } else if (fc_item_grp == 2) { // "Font style:"
          fc_item2_left = cs->x;  // to identify "Sample" frame
          fc_item_gap = cs->x - fc_item1_right;
        } else if (fc_item_grp == 3) { // "Size:"
          fc_item_right = cs->x + cs->cx;
        } else if (fc_item_grp == 6 || (fc_item_grp < 6 && cs->x == fc_item2_left)) {
          // "Sample" frame
          fc_sample_left = cs->x;
          fc_sample_bottom = cs->y + cs->cy;
          if (!(adjust_sample & 1)) {
            cs->x = fc_item_left;
            //cs->cx = fc_item_right - fc_item_left;  // align with "Size:"
            cs->cx = fc_width - 2 * fc_item_left;
            if (adjust_sample_plus)
              cs->cx += fc_item_gap + fc_button_width;  // not yet known
          }
        }
      }
      else if ((cs->style & WS_CHILD) && !(cs->style & WS_GROUP)) {
        fc_item_idx ++;
        if (fc_item_grp == 1 && fc_item_idx == 1) { // font list
          fc_item_left = cs->x;
          fc_item1_right = cs->x + cs->cx;
        }
        else if (fc_item_grp == 3 && fc_item_idx == 1) { // size list
          fc_item_right = cs->x + cs->cx;
        }
      }
    }
    else if (!font_chooser) {
      float zoom = 1.25;
      static int cc_item_grp = 0;
      static int cc_item_left = 6;
      static int cc_height = 326;
      static int cc_width = 453;
      static int cc_vert = 453 / 2;
      static int cc_hue_shift = 6;

      if ((cs->style & WS_CHILD) && (cs->style & WS_GROUP)) {
        cc_item_grp ++;
      }
      if (cc_item_grp == 1)
        cc_item_left = cs->x;

      if (!(cs->style & WS_CHILD)) {
        // colour chooser popup dialog
        cc_item_grp = 0;
        cc_height = cs->cy;
        cc_width = cs->cx;
        cc_vert = cc_width / 2;
        cs->cx = cc_vert + (cs->cx - cc_vert) * zoom;
      }
      else if (cs->x >= cc_vert && cs->y > cc_height / 2) {
        if (cc_item_grp >= 12 && cc_item_grp <= 23) {
          // Hue etc
          if (!(cs->style & WS_TABSTOP)) {
            // labels
            float zoomx = (cc_item_grp < 18) ? zoom * 1.05 : zoom;
            int cc_hue_right = cs->x + cs->cx;
            cs->x = cc_vert + (cs->x - cc_vert) * zoomx;
#if defined(debug_dialog_hook) && debug_dialog_hook > 1
            printf("%d %f %f x %d->%d |%d\n", cc_item_grp, zoom, zoomx, cc_hue_right - cs->cx, cs->x, cc_hue_right);
#endif
            cs->cx *= zoom;
            cs->cx += 2 * cc_item_left;
            if (cc_item_grp % 6 == 0)
              cc_hue_shift = cs->x + cs->cx - cc_hue_right;
          }
          else {
            // value fields
            cs->x += cc_hue_shift;
#if defined(debug_dialog_hook) && debug_dialog_hook > 1
            printf("%d x %d->%d\n", cc_item_grp, cs->x - cc_hue_shift, cs->x);
#endif
          }
        }
        else if (!(cs->style & WS_TABSTOP)) {
          // Colour|Solid box and labels
          cs->x = cc_vert + (cs->x - cc_vert) * zoom;
          cs->cx *= zoom;
        }
      }
    }
  }
  else if (nCode == HCBT_ACTIVATE) {
    setlabel(IDOK, _W("OK"));
    setlabel(IDCANCEL, _W("Cancel"));

    if (localize && GetDlgItem((HWND)wParam, 1088))
      //__ Font chooser: title bar label
      SetWindowTextW((HWND)wParam, _W("Font "));
    //__ Font chooser: button
    setlabel(1026, _W("&Apply"));
    //__ Font chooser:
    setlabel(1088, _W("&Font:"));
    //__ Font chooser:
    setlabel(1089, _W("Font st&yle:"));
    //__ Font chooser:
    setlabel(1090, _W("&Size:"));
    //__ Font chooser:
    setlabel(1073, _W("Sample"));

    font_sample = GetDlgItem((HWND)wParam, 1092);
    //__ Font chooser: text sample ("AaBbYyZz" by default)
    SetWindowTextW(font_sample, *new_cfg.font_sample ? new_cfg.font_sample : _W("Ferqœm’4€"));
    // if we manage to get the field longer, 
    // sample text could be picked from http://clagnut.com/blog/2380/,
    // e.g. "Cwm fjord bank glyphs vext quiz"

    HWND away = 0;
    if (new_cfg.fontmenu & 8) {
      // Font chooser: remove "Script:" junk:
      away = GetDlgItem((HWND)wParam, 1094);
      if (away)
        DestroyWindow(away);
      away = GetDlgItem((HWND)wParam, 1140);
      if (away)
        DestroyWindow(away);
    }
    else {
      //__ Font chooser: this field is only shown with FontMenu=1
      setlabel(1094, _W("Sc&ript:"));
      //__ Font chooser: this field is only shown with FontMenu=1
      setlabel(1592, _W("<A>Show more fonts</A>"));
    }

    if (localize && GetDlgItem((HWND)wParam, 730))
      //__ Colour chooser: title bar label
      SetWindowTextW((HWND)wParam, _W("Colour "));
    // tricky way to adjust "Basic colors:" and "Custom colors:" labels 
    // which insanely have the same dialog item ID, see
    // http://www.xtremevbtalk.com/api/181863-changing-custom-color-label-choosecolor-dialog-comdlg32-dll.html
    HWND basic_colors = GetDlgItem((HWND)wParam, 65535);
    //static HWND custom_colors = 0;  // previously seen "Custom colors:" item
    if (basic_colors && !hooked_window_activated) {
      // alternatives to distinguish re-focussing from initial activation:
      // if (basic_colors && basic_colors != custom_colors)
      //   but that could fail in rare cases
      // if (!((CBTACTIVATESTRUCT *)lParam)->fMouse)
      //   but that fails if re-focussing is done without mouse (e.g. Alt+TAB)
#ifdef debug_dialog_hook
      trace_label(65535, basic_colors, _W("B&asic colours:"));
#endif
      wchar * lbl = null;
      if (!localize) {
        int size = GetWindowTextLengthW(basic_colors) + 1;
        lbl = newn(wchar, size);
        GetWindowTextW(basic_colors, lbl, size);
      }

      WPARAM fnt = diafont() ?: (WPARAM)SendMessage(basic_colors, WM_GETFONT, 0, 0);
      DestroyWindow(basic_colors);
      //__ Colour chooser:
      basic_colors = CreateWindowExW(4, W("Static"), lbl ?: _W("B&asic colours:"), 0x50020000, 6, 7, 210, 15, (HWND)wParam, 0, inst, 0);
                         //shortkey disambiguated from original "&Basic colors:"
      // this does not seem to apply dark mode to anything
      //win_dark_mode(basic_colors);
      SendMessage(basic_colors, WM_SETFONT, fnt, MAKELPARAM(true, 0));
      if (lbl)
        free(lbl);
      //custom_colors = GetDlgItem((HWND)wParam, 65535);
      //__ Colour chooser:
      setlabel(65535, _W("&Custom colours:"));
      // drop disabled "Define Custom Colours >>"
      away = GetDlgItem((HWND)wParam, 719);
      if (away)
        DestroyWindow(away);
    }
    //__ Colour chooser:
    setlabel(719, _W("De&fine Custom Colours >>"));
          //shortkey disambiguated from original "&Define Custom Colours >>"
    //__ Colour chooser:
    setlabel(730, _W("Colour"));
    //__ Colour chooser:
    setlabel(731, _W("|S&olid"));
    //__ Colour chooser:
    setlabel(723, _W("&Hue:"));
            //shortkey disambiguated from original "Hu&e:"
    //__ Colour chooser:
    setlabel(724, _W("&Sat:"));
    //__ Colour chooser:
    setlabel(725, _W("&Lum:"));
    //__ Colour chooser:
    setlabel(726, _W("&Red:"));
    //__ Colour chooser:
    setlabel(727, _W("&Green:"));
    //__ Colour chooser:
    setlabel(728, _W("&Blue:"));
            //shortkey disambiguated from original "Bl&ue:"
    //__ Colour chooser:
    setlabel(712, _W("A&dd to Custom Colours"));
            //shortkey disambiguated from original "&Add to Custom Colours"

#if defined(debug_dialog_hook) && debug_dialog_hook > 1
    for (int id = 12; id < 65536; id++) {
      HWND dlg = GetDlgItem((HWND)wParam, id);
      if (dlg) {
        wchar buf [99];
        RECT r;
        GetWindowRect(dlg, &r);
        trace_hook("dlgitem %5d: %4d %4d %4d %4d / ", id, (int)r.left, (int)r.top, (int)r.right, (int)r.bottom);
        GetClientRect(dlg, &r);
        trace_hook("%d %d %3d %3d ", (int)r.left, (int)r.top, (int)r.right, (int)r.bottom);
        GetWindowTextW(dlg, buf, 99);
        trace_hook("<%ls>\n", buf);
      }
    }
#endif

#ifdef post_adjust_sample
    // resize frame around sample, try to resize text sample (failed)
    if (font_chooser && adjust_sample && (new_cfg.fontmenu & 8)) {
      HWND sample = GetDlgItem((HWND)wParam, 1073);  // "Sample" frame
      if ((adjust_sample & 1) && sample) {
        // adjust frame and label "Sample"
        RECT wr;
        GetWindowRect(sample, &wr);
        POINT wp;
        wp.x = wr.left;
        wp.y = wr.top;
        ScreenToClient((HWND)wParam, &wp);
        trace_hook(" Sample: %4d/%3d %4d/%3d %4d %4d\n", wr.left, wp.x, wr.top, wp.y, wr.right, wr.bottom);
        SetWindowPos(sample, null, fc_item_left, wp.y,
                     fc_item_right - fc_item_left, wr.bottom - wr.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
      }

      sample = GetDlgItem((HWND)wParam, 1092);  // sample text
      if ((adjust_sample & 2) && sample) {
        // try to adjust sample text;
        // we can move/resize the labelled frame,
        // so why can't we adjust the sample text?
        RECT wr;
        GetWindowRect(sample, &wr);
        POINT wp;
        wp.x = wr.left;
        wp.y = wr.top;
        ScreenToClient((HWND)wParam, &wp);
        trace_hook(" sample: %4d/%3d %4d/%3d %4d %4d\n", wr.left, wp.x, wr.top, wp.y, wr.right, wr.bottom);
        SetWindowPos(sample, null, fc_item_left, wr.top,
                     fc_item_right - fc_item_left - 2 * fc_sample_gap, wr.bottom - wr.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
      }
    }
#endif

    // crop dialog size after removing useless stuff
    if (font_chooser && (new_cfg.fontmenu & 8) && away) {
      // used to be if (... && GetDlgItem((HWND)wParam, 1092))
      RECT wr;
      GetWindowRect((HWND)wParam, &wr);
      RECT cr;
      GetClientRect((HWND)wParam, &cr);
      int vborder = (wr.bottom - wr.top) - (cr.bottom - cr.top);
      trace_hook("Chooser: %4d %4d %4d %4d / %d %d %3d %3d\n", (int)wr.left, (int)wr.top, (int)wr.right, (int)wr.bottom, (int)cr.left, (int)cr.top, (int)cr.right, (int)cr.bottom);
      SetWindowPos((HWND)wParam, null, 0, 0,
                   wr.right - wr.left, fc_sample_bottom + fc_sample_gap + vborder,
                   SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOZORDER);
    }

    hooked_window_activated = true;
  }

#ifdef debug_dlgitems
    char * s = getenv("s");
    int n;
    while (s && (n = strtol(s, &s, 0))) {
      printf("?%d\n", n);
      setfont(n);
    }
#endif
    // adjust custom font for anonymous system menu fields
    setfont(703);  // Hue
    setfont(704);  // Sat
    setfont(705);  // Lum
    setfont(706);  // Red
    setfont(707);  // Green
    setfont(708);  // Blue
    setfont(1136);  // Font
    setfont(1137);  // Font style
    setfont(1138);  // Size

  if (do_next_hook) {
    trace_hook(" -> NextHook\n");
    LRESULT ret = CallNextHookEx(0, nCode, wParam, lParam);
    trace_hook(" <- NextHook %ld\n", (long)ret);
    return ret;
  }
  else
    return 0;  // 0: let default dialog box procedure process the message
}

static LRESULT CALLBACK
set_labels_fc(int nCode, WPARAM wParam, LPARAM lParam)
{
  return set_labels(true, nCode, wParam, lParam);
}

static LRESULT CALLBACK
set_labels_cc(int nCode, WPARAM wParam, LPARAM lParam)
{
  return set_labels(false, nCode, wParam, lParam);
}


#ifndef CF_INACTIVEFONTS
# ifdef __MSABI_LONG
#define CF_INACTIVEFONTS __MSABI_LONG (0x02000000)
# else
#define CF_INACTIVEFONTS 0x02000000
# endif
#endif

#define dont_debug_fontsel

#ifdef debug_fontsel
#define trace_fontsel(params)	printf params
#else
#define trace_fontsel(params)	
#endif

static winctrl * font_ctrl;

#define dont_debug_messages

static UINT_PTR CALLBACK
fonthook(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  (void)lParam;
#ifdef debug_messages
#include <time.h>
  static struct {
  uint wm_;
  char * wm_name;
  } wm_names[] = {
#  include "_wm.t"
  };
  char * wm_name = "WM_?";
  if ((msg != WM_SETCURSOR && msg != WM_NCHITTEST && msg != WM_MOUSEMOVE
       && msg != WM_ERASEBKGND && msg != WM_CTLCOLORDLG && msg != WM_PRINTCLIENT && msg != WM_CTLCOLORBTN
       && msg != WM_ENTERIDLE
       && (msg != WM_NOTIFY)
     )) {
    for (uint i = 0; i < lengthof(wm_names); i++)
      if (msg == wm_names[i].wm_ && !strstr(wm_names[i].wm_name, "FIRST")) {
        wm_name = wm_names[i].wm_name;
        break;
      }
    printf("[%d] fonthook %04X %s (%04X %08X)\n", (int)time(0), msg, wm_name, (unsigned)wParam, (unsigned)lParam);
  }
#endif

  if (msg == WM_DRAWITEM) {
    // restore our own font sample text
#define disp ((DRAWITEMSTRUCT*)lParam)
#ifdef debug_messages
    printf("                           %04X %d %2d %04X %04X %p\n",
           disp->CtlType, disp->CtlID, disp->itemID, disp->itemAction, disp->itemState, 
           disp->hwndItem);
#endif
    if (disp->CtlID == 1137 && (disp->itemAction == ODA_SELECT))
      // or any of CtlID=1137 (font style)/itemID=0...
      // or CtlID=1138 (font size)/itemID=2... will do
      SetWindowTextW(font_sample, *new_cfg.font_sample ? new_cfg.font_sample : _W("Ferqœm’4€"));
  }

  //winctrl * c = (winctrl *)lParam;  // does not work
  winctrl * c = font_ctrl;
  if (msg == WM_COMMAND && wParam == 1026) {  // Apply
    LOGFONTW lfapply;
    SendMessageW(hdlg, WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM)&lfapply);

    font_spec * fsp = &new_cfg.font;
    // we must not realloc a copied string pointer,
    // -- wstrset(&fsp->name, lfapply.lfFaceName); --
    // let's rather dup the string;
    // if we just try to resync the copies 
    // -- ((font_spec *) c->data)->name = fsp->name; --
    // that would heal the case "Apply", then "Cancel", 
    // but not the case "Apply", then "OK",
    // so let's rather accept a negligable memory leak here
    // (few bytes per "Apply" click)
    fsp->name = wcsdup(lfapply.lfFaceName);

    HDC dc = GetDC(wnd);
    fsp->size = -MulDiv(lfapply.lfHeight, 72, GetDeviceCaps(dc, LOGPIXELSY));
    trace_fontsel(("Apply lfHeight %ld -> size %d <%ls>\n", (long int)lfapply.lfHeight, fsp->size, lfapply.lfFaceName));
    ReleaseDC(0, dc);
    fsp->weight = lfapply.lfWeight;
    fsp->isbold = (lfapply.lfWeight >= FW_BOLD);
    // apply font
    apply_config(false);
    // update font spec label
    c->ctrl->handler(c->ctrl, EVENT_REFRESH);  // -> dlg_stdfontsel_handler
    //or dlg_fontsel_set(c->ctrl, fsp);
  }
  else if (msg == WM_COMMAND && wParam == 1) {  // OK
#ifdef failed_workaround_for_no_font_issue
    /*
      Trying to work-around issue #507
      "There is no font with that name."
      "Choose a font from the list of fonts."
      as it occurs with Meslo LG L DZ for Powerline
    */
    LOGFONTW lfapply;
    SendMessageW(hdlg, WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM)&lfapply);
    // lfapply.lfFaceName is "Meslo LG L DZ for Powerline"
    HWND wnd = GetDlgItem(hdlg, c->base_id +99);
    int size = GetWindowTextLengthW(wnd) + 1;
    wchar * fn = newn(wchar, size);
    GetWindowTextW(wnd, fn, size);
    // fn is "Meslo LG L DZ for Powerline RegularForPowerline"
    // trying to fix the inconsistency with
    SetWindowTextW(wnd, lfapply.lfFaceName);
    // does not help...
    // what a crap!
#endif
  }
  else if (msg == WM_COMMAND && wParam == 2) {  // Cancel
  }
  return 0;  // default processing
}

static void
select_font(winctrl *c)
{
  font_spec fs = *(font_spec *) c->data;
  LOGFONTW lf;
  HDC dc = GetDC(wnd);
 /* We could have the idea to consider `dpi` here, like for MulDiv in 
  * win_init_fonts, but that's wrong.
  */
  lf.lfHeight = -MulDiv(fs.size, GetDeviceCaps(dc, LOGPIXELSY), 72);
  trace_fontsel(("Choose size (%d) %d -> lfHeight %ld\n", cfg.font.size, fs.size, (long int)lf.lfHeight));
  ReleaseDC(0, dc);
  lf.lfWidth = lf.lfEscapement = lf.lfOrientation = 0;
  lf.lfItalic = lf.lfUnderline = lf.lfStrikeOut = 0;
  lf.lfWeight = fs.weight ? fs.weight : fs.isbold ? FW_BOLD : 0;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = DEFAULT_QUALITY;
  lf.lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
  if (wcslen(fs.name) < lengthof(lf.lfFaceName))
    wcscpy(lf.lfFaceName, fs.name);
  else
    lf.lfFaceName[0] = 0;

  CHOOSEFONTW cf;
  cf.lStructSize = sizeof(cf);
  cf.hwndOwner = dlg.wnd;
  cf.lpLogFont = &lf;
  cf.lpfnHook = fonthook;
  cf.lCustData = (LPARAM)c;  // does not work
  font_ctrl = c;
  cf.Flags =
    CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST
    | CF_FIXEDPITCHONLY | CF_NOVERTFONTS
    | CF_SCREENFONTS  // needed on Windows XP
    | CF_NOSCRIPTSEL
    | CF_APPLY | CF_ENABLEHOOK  // enable Apply button
    ;
  if (new_cfg.show_hidden_fonts)
    // include fonts marked to Hide in Fonts Control Panel
    cf.Flags |= CF_INACTIVEFONTS;
  else
    cf.Flags |= CF_SCRIPTSONLY; // exclude fonts with OEM or SYMBOL charset
  if (new_cfg.fontmenu == 1)  // previously OldFontMenu
    cf.Flags =
      CF_FIXEDPITCHONLY | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT |
      CF_SCREENFONTS | CF_NOSCRIPTSEL;

  // open font selection menu
  if (new_cfg.fontmenu & 2)
    hook_windows(set_labels_fc);
  bool ok = ChooseFontW(&cf);
  if (new_cfg.fontmenu & 2)
    unhook_windows();
#ifdef debug_fonthook
  font_spec * fsp = (font_spec *) c->data;
  printf("ok %d c->data %p <%ls> copy %p <%ls> -> <%ls>\n", ok, fsp->name, fsp->name, fs.name, fs.name, lf.lfFaceName);
#endif
  if (ok) {
    // font selection menu closed with OK
    wstrset(&fs.name, lf.lfFaceName);
    // here we could enumerate font widths and adjust...
    // rather doing that in win_init_fonts
    fs.size = cf.iPointSize / 10;
    trace_fontsel(("OK iPointSize %d -> size %d <%ls>\n", cf.iPointSize, fs.size, lf.lfFaceName));
    fs.weight = lf.lfWeight;
    fs.isbold = (lf.lfWeight >= FW_BOLD);
    dlg_fontsel_set(c->ctrl, &fs);
    //call dlg_stdfontsel_handler
    c->ctrl->handler(c->ctrl, EVENT_VALCHANGE);
  }
}

void
dlg_text_paint(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;

  static HFONT fnt = 0;
  if (fnt)
    DeleteObject(fnt);

  int font_height;
  int size = new_cfg.font.size;
  // dup'ed from win_init_fonts()
  HDC dc = GetDC(wnd);
  if (cfg.handle_dpichanged && per_monitor_dpi_aware)
    font_height =
      size > 0 ? -MulDiv(size, dpi, 72) : -size;
      // dpi is determined initially and via WM_WINDOWPOSCHANGED;
      // if WM_DPICHANGED were used, this would need to be modified
  else
    font_height =
      size > 0 ? -MulDiv(size, GetDeviceCaps(dc, LOGPIXELSY), 72) : -size;
  ReleaseDC(wnd, dc);

  fnt = CreateFontW(font_height, 0, 0, 0, new_cfg.font.weight, 
                    false, false, false,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, FIXED_PITCH | FF_DONTCARE,
                    new_cfg.font.name);
  if (ctrl->type == CTRL_BUTTON) {
    HWND wnd = GetDlgItem(dlg.wnd, c->base_id);
    SendMessage(wnd, WM_SETFONT, (WPARAM)fnt, MAKELPARAM(false, 0));
    SetWindowTextW(wnd, *new_cfg.font_sample ? new_cfg.font_sample : _W("Ferqœm’4€"));
  }
  DeleteObject(fnt);
}

/*
 * The dialog-box procedure calls this function to handle Windows
 * messages on a control we manage.
 */
int
winctrl_handle_command(UINT msg, WPARAM wParam, LPARAM lParam)
{
  winctrl *c;
  control *ctrl;
  int i, id, ret;

 /*
  * Look up the control ID in our data.
  */
  c = null;
  for (i = 0; i < dlg.nctrltrees; i++) {
    c = winctrl_findbyid(dlg.controltrees[i], LOWORD(wParam));
    if (c)
      break;
  }
  if (!c)
    return 0;   /* we have nothing to do */

  if (msg == WM_DRAWITEM) {
   /*
    * Owner-draw request for a panel title.
    */
    LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT) lParam;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    SIZE s;

    SetMapMode(dc, MM_TEXT);   /* ensure logical units == pixels */

    GetTextExtentPoint32A(dc, (char *) c->data, strlen((char *) c->data), &s);
    DrawEdge(dc, &r, EDGE_ETCHED, BF_ADJUST | BF_RECT);
    string text = (string) c->data;
    if (nonascii(text)) {
      // assuming that the panel title is stored in UTF-8,
      // transform it for proper Windows display
      wchar * us = cs__utftowcs(text);
      TextOutW(dc, r.left + (r.right - r.left - s.cx) / 2,
               r.top + (r.bottom - r.top - s.cy) / 2, us, wcslen(us));
      free(us);
    }
    else
      TextOutA(dc, r.left + (r.right - r.left - s.cx) / 2,
               r.top + (r.bottom - r.top - s.cy) / 2, text, strlen(text));

    return true;
  }

  ctrl = c->ctrl;
  id = LOWORD(wParam) - c->base_id;

  if (!ctrl || !ctrl->handler)
    return 0;   /* nothing we can do here */

 /*
  * From here on we do not issue `return' statements until the
  * very end of the dialog box: any event handler is entitled to
  * ask for a colour selector, so we _must_ always allow control
  * to reach the end of this switch statement so that the
  * subsequent code can test dlg.coloursel_wanted().
  */
  ret = 0;
  dlg.coloursel_wanted = false;

 /*
  * Now switch on the control type and the message.
  */
  if (msg == WM_COMMAND) {
    WORD note = HIWORD(wParam);
    switch (ctrl->type) {
      when CTRL_RADIO:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
           /*
            * We sometimes get spurious BN_CLICKED messages for the
            * radio button that is just about to _lose_ selection, if
            * we're switching using the arrow keys. Therefore we
            * double-check that the button in wParam is actually
            * checked before generating an event.
            */
            if (IsDlgButtonChecked(dlg.wnd, LOWORD(wParam)))
              ctrl->handler(ctrl, EVENT_VALCHANGE);
        }
      when CTRL_CHECKBOX:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
            ctrl->handler(ctrl, EVENT_VALCHANGE);
        }
      when CTRL_BUTTON:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
            ctrl->handler(ctrl, EVENT_ACTION);
          when BN_PAINT:  // unused
            dlg_text_paint(ctrl);
        }
      when CTRL_FONTSELECT:
        if (id == 2) {
          switch (note) {
            when BN_SETFOCUS or BN_KILLFOCUS:
              winctrl_set_focus(ctrl, note == BN_SETFOCUS);
            when BN_CLICKED or BN_DOUBLECLICKED:
              select_font(c);
          }
        }
      when CTRL_LISTBOX:
        if (ctrl->listbox.height != 0 &&
            (note == LBN_SETFOCUS || note == LBN_KILLFOCUS))
          winctrl_set_focus(ctrl, note == LBN_SETFOCUS);
        else if (ctrl->listbox.height == 0 &&
            (note == CBN_SETFOCUS || note == CBN_KILLFOCUS))
          winctrl_set_focus(ctrl, note == CBN_SETFOCUS);
        else if (id >= 2 && (note == BN_SETFOCUS || note == BN_KILLFOCUS))
          winctrl_set_focus(ctrl, note == BN_SETFOCUS);
        else if (note == LBN_DBLCLK) {
          SetCapture(dlg.wnd);
          ctrl->handler(ctrl, EVENT_ACTION);
        }
        else if (note == LBN_SELCHANGE)
          ctrl->handler(ctrl, EVENT_SELCHANGE);
      when CTRL_EDITBOX:
        if (ctrl->editbox.has_list) {
          switch (note) {
            when CBN_SETFOCUS:
              winctrl_set_focus(ctrl, true);
            when CBN_KILLFOCUS:
              winctrl_set_focus(ctrl, false);
              ctrl->handler(ctrl, EVENT_UNFOCUS);
            when CBN_SELCHANGE: {
              int index = SendDlgItemMessage(
                            dlg.wnd, c->base_id + 1, CB_GETCURSEL, 0, 0);
              int wlen = SendDlgItemMessageW(
                          dlg.wnd, c->base_id + 1, CB_GETLBTEXTLEN, index, 0);
              wchar wtext[wlen + 1];
              SendDlgItemMessageW(
                dlg.wnd, c->base_id + 1, CB_GETLBTEXT, index, (LPARAM) wtext);
              SetDlgItemTextW(dlg.wnd, c->base_id + 1, wtext);
              ctrl->handler(ctrl, EVENT_SELCHANGE);
            }
            when CBN_EDITCHANGE:
              ctrl->handler(ctrl, EVENT_VALCHANGE);
          }
        }
        else {
          switch (note) {
            when EN_SETFOCUS:
              winctrl_set_focus(ctrl, true);
            when EN_KILLFOCUS:
              winctrl_set_focus(ctrl, false);
              ctrl->handler(ctrl, EVENT_UNFOCUS);
            when EN_CHANGE:
              ctrl->handler(ctrl, EVENT_VALCHANGE);
          }
        }
    }
  }

 /*
  * If the above event handler has asked for a colour selector,
  * now is the time to generate one.
  */
  if (dlg.coloursel_wanted) {
    static CHOOSECOLOR cc;
    static DWORD custom[16] = { 0 };    /* zero initialisers */
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = dlg.wnd;
    cc.hInstance = (HWND) inst;
    cc.lpCustColors = custom;
    cc.rgbResult = dlg.coloursel_result;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    hook_windows(set_labels_cc);
    dlg.coloursel_ok = ChooseColor(&cc);
    unhook_windows();
    dlg.coloursel_result = cc.rgbResult;
    ctrl->handler(ctrl, EVENT_CALLBACK);
  }

  return ret;
}

/*
 * Now the various functions that the platform-independent
 * mechanism can call to access the dialog box entries.
 */

void
dlg_radiobutton_set(control *ctrl, int whichbutton)
{
  winctrl *c = ctrl->plat_ctrl;
  if (c) {  // can be null (leaving the dialog with ESC from another control)
    assert(c && c->ctrl->type == CTRL_RADIO);
    CheckRadioButton(dlg.wnd, c->base_id + 1,
                     c->base_id + c->ctrl->radio.nbuttons,
                     c->base_id + 1 + whichbutton);
  }
}

int
dlg_radiobutton_get(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;
  int i;
  assert(c && c->ctrl->type == CTRL_RADIO);
  for (i = 0; i < c->ctrl->radio.nbuttons; i++)
    if (IsDlgButtonChecked(dlg.wnd, c->base_id + 1 + i))
      return i;
  assert(!"No radio button was checked?!");
  return 0;
}

void
dlg_checkbox_set(control *ctrl, bool checked)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c && c->ctrl->type == CTRL_CHECKBOX);
  CheckDlgButton(dlg.wnd, c->base_id, checked);
}

bool
dlg_checkbox_get(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c && c->ctrl->type == CTRL_CHECKBOX);
  return IsDlgButtonChecked(dlg.wnd, c->base_id);
}

void
dlg_editbox_set(control *ctrl, string text)
{
  if (nonascii(text)) {
    // transform item for proper Windows display
    wchar * us = cs__utftowcs(text);
    dlg_editbox_set_w(ctrl, us);
    free(us);
    return;
  }

  winctrl *c = ctrl->plat_ctrl;
  assert(c && c->ctrl->type == CTRL_EDITBOX);
  SetDlgItemTextA(dlg.wnd, c->base_id + 1, text);
}

void
dlg_editbox_set_w(control *ctrl, wstring text)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c &&
         (c->ctrl->type == CTRL_EDITBOX
         ||c->ctrl->type == CTRL_LISTBOX));
  if (c->ctrl->type != CTRL_LISTBOX) {
    SetDlgItemTextW(dlg.wnd, c->base_id + 1, text);
  }
  else {
    HWND wnd = GetDlgItem(dlg.wnd, c->base_id + 1);
    int len = wcslen(text);
    wchar * buf = newn(wchar, len + 1);
    int n = SendMessageW(wnd, LB_GETCOUNT, 0, (LPARAM)0);
    for (int i = 0; i < n; i++) {
      int ilen = SendMessageW(wnd, LB_GETTEXTLEN, i, (LPARAM)0);
      if (ilen > len) {
        buf = renewn(buf, ilen + 1);
        len = ilen;
      }
      SendMessageW(wnd, LB_GETTEXT, i, (LPARAM)buf);
      if (wcscmp(buf, text) == 0) {
        SendMessageW(wnd, LB_SETCURSEL, i, (LPARAM)0);
        break;
      }
    }
    free(buf);
  }
}

void
dlg_editbox_get(control *ctrl, string *text_p)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c &&
         (c->ctrl->type == CTRL_EDITBOX
         ||c->ctrl->type == CTRL_LISTBOX));
  HWND wnd = GetDlgItem(dlg.wnd, c->base_id + 1);
  int size = GetWindowTextLength(wnd) + 1;
  char *text = (char *)*text_p;
  text = renewn(text, size);
  GetWindowTextA(wnd, text, size);
  *text_p = text;
}

void
dlg_editbox_get_w(control *ctrl, wstring *text_p)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c &&
         (c->ctrl->type == CTRL_EDITBOX
         ||c->ctrl->type == CTRL_LISTBOX));
  HWND wnd = GetDlgItem(dlg.wnd, c->base_id + 1);
  wchar *text = (wchar *)*text_p;
  if (c->ctrl->type != CTRL_LISTBOX) {
    // handle single-line editbox (with optional popup list)
    int size = GetWindowTextLengthW(wnd) + 1;
    text = renewn(text, size);
    // In the popup editbox (combobox), 
    // Windows goofs up non-ANSI characters here unless the 
    // WM_COMMAND dialog callback function winctrl_handle_command above 
    // (case CBN_SELCHANGE) also uses the Unicode function versions 
    // SendDlgItemMessageW and SetDlgItemTextW
    GetWindowTextW(wnd, text, size);
    //GetDlgItemTextW(dlg.wnd, c->base_id + 1, text, size);  // same
  }
  else {
    // handle multi-line listbox
    int n = SendMessageW(wnd, LB_GETCURSEL, 0, (LPARAM)0);
    int len = SendMessageW(wnd, LB_GETTEXTLEN, n, (LPARAM)0);
    text = renewn(text, len + 1);
    SendMessageW(wnd, LB_GETTEXT, n, (LPARAM)text);
  }
  *text_p = text;
}

/* The `listbox' functions also apply to combo boxes. */
void
dlg_listbox_clear(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;
  int msg;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  msg = (c->ctrl->type == CTRL_LISTBOX &&
         c->ctrl->listbox.height != 0 ? LB_RESETCONTENT : CB_RESETCONTENT);
  SendDlgItemMessage(dlg.wnd, c->base_id + 1, msg, 0, 0);
}

void
dlg_listbox_add(control *ctrl, string text)
{
  if (nonascii(text)) {
    // transform item for proper Windows display
    wchar * us = cs__utftowcs(text);
    dlg_listbox_add_w(ctrl, us);
    free(us);
    return;
  }

  winctrl *c = ctrl->plat_ctrl;
  int msg;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  msg = (c->ctrl->type == CTRL_LISTBOX &&
         c->ctrl->listbox.height != 0 ? LB_ADDSTRING : CB_ADDSTRING);
  SendDlgItemMessage(dlg.wnd, c->base_id + 1, msg, 0, (LPARAM) text);
}

void
dlg_listbox_add_w(control *ctrl, wstring text)
{
  winctrl *c = ctrl->plat_ctrl;
  int msg;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  msg = (c->ctrl->type == CTRL_LISTBOX &&
         c->ctrl->listbox.height != 0 ? LB_ADDSTRING : CB_ADDSTRING);
  SendDlgItemMessageW(dlg.wnd, c->base_id + 1, msg, 0, (LPARAM) text);
}

int
dlg_listbox_getcur(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  int idx;
  if (c->ctrl->type == CTRL_LISTBOX)
    idx = SendDlgItemMessage(dlg.wnd, c->base_id + 1, LB_GETCURSEL, 0, 0);
  else
    idx = SendDlgItemMessage(dlg.wnd, c->base_id + 1, CB_GETCURSEL, 0, 0);
  //HWND wnd = GetDlgItem(dlg.wnd, c->base_id + 1);
  //idx = SendMessage(wnd, LB_/CB_GETCURSEL, 0, (LPARAM)0);
  return idx;
}

void
dlg_fontsel_set(control *ctrl, font_spec *fs)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c && c->ctrl->type == CTRL_FONTSELECT);

  trace_fontsel(("fontsel_set <%ls>\n", fs->name));
  *(font_spec *) c->data = *fs;   /* structure copy */

  static char * boldnesses[] = {
    "Thin, ",
    "Extralight, ",
    "Light, ",
    "",
    "Medium, ",
    "Semibold, ",
    "Bold, ",
    "Extrabold, ",
    "Heavy, "
  };

  //int boldness = (fs->weight - 1) / 111;
  int boldness = (fs->weight - 50) / 100;
  if (boldness < 0)
    boldness = 0;
  else if (boldness >= (int)lengthof(boldnesses))
    boldness = lengthof(boldnesses) - 1;

  //char * boldstr = fs->isbold ? "bold, " : "";
  char * boldstr = boldnesses[boldness];
  char * stylestr =
    fs->size ? asform(", %s%d%s", boldstr, abs(fs->size), fs->size < 0 ? "px" : "pt")
             : asform(", %sdefault size", fs->name, boldstr);
  wchar * wstylestr = cs__utftowcs(stylestr);
  int wsize = wcslen(fs->name) + wcslen(wstylestr) + 1;
  wchar * wbuf = newn(wchar, wsize);
  wcscpy(wbuf, fs->name);
  wcscat(wbuf, wstylestr);
  SetDlgItemTextW(dlg.wnd, c->base_id + 1, wbuf);
  free(wbuf);
  free(wstylestr);
  free(stylestr);
}

void
dlg_fontsel_get(control *ctrl, font_spec *fs)
{
  winctrl *c = ctrl->plat_ctrl;
  assert(c && c->ctrl->type == CTRL_FONTSELECT);
  trace_fontsel(("fontsel_get <%ls>\n", ((font_spec*)c->data)->name));
  *fs = *(font_spec *) c->data;  /* structure copy */
}

void
dlg_set_focus(control *ctrl)
{
  winctrl *c = ctrl->plat_ctrl;
  int id;
  switch (ctrl->type) {
    when CTRL_EDITBOX or CTRL_LISTBOX: id = c->base_id + 1;
    when CTRL_FONTSELECT: id = c->base_id + 2;
    when CTRL_RADIO:
      id = c->base_id + ctrl->radio.nbuttons;
      while (id > 1 && IsDlgButtonChecked(dlg.wnd, id))
        --id;
     /*
      * In the theoretically-unlikely case that no button was selected, 
      * id should come out of this as 1, which is a reasonable enough choice.
      */
    otherwise: id = c->base_id;
  }
  SetFocus(GetDlgItem(dlg.wnd, id));
}

/*
 * This function signals to the front end that the dialog's processing is 
 * completed, and passes an integer value (typically a success status).
 */
void
dlg_end(void)
{
  dlg.ended = true;
}

void
dlg_refresh(control *ctrl)
{
  if (!ctrl) {
   /*
    * Send EVENT_REFRESH to absolutely everything.
    */
    for (int i = 0; i < dlg.nctrltrees; i++) {
      for (winctrl *c = dlg.controltrees[i]->first; c; c = c->next) {
        if (c->ctrl && c->ctrl->handler) {
          c->ctrl->handler(c->ctrl, EVENT_REFRESH);
        }
      }
    }
  }
  else {
   /*
    * Send EVENT_REFRESH to a specific control.
    */
    if (ctrl->handler != null)
      ctrl->handler(ctrl, EVENT_REFRESH);
  }
}

void
dlg_coloursel_start(colour c)
{
  dlg.coloursel_wanted = true;
  dlg.coloursel_result = c;
}

int
dlg_coloursel_results(colour *cp)
{
  bool ok = dlg.coloursel_ok;
  if (ok)
    *cp = dlg.coloursel_result;
  return ok;
}

void
windlg_init(void)
{
  dlg.nctrltrees = 0;
  dlg.ended = false;
  dlg.focused = null;
  dlg.wnd = null;
}

void
windlg_add_tree(winctrls * wc)
{
  assert(dlg.nctrltrees < (int) lengthof(dlg.controltrees));
  dlg.controltrees[dlg.nctrltrees++] = wc;
}
