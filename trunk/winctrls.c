// winctrls.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winctrls.h"

#include "winpriv.h"

#define _RPCNDR_H
#define _WTYPES_H
#define _OLE2_H
#include <commdlg.h>

/*
 * winctrls.c: routines to self-manage the controls in a dialog
 * box.
 */

/*
 * Possible TODO in new cross-platform config box stuff:
 *
 *  - When lining up two controls alongside each other, I wonder if
 *    we could conveniently arrange to centre them vertically?
 *    Particularly ugly in the current setup is the `Add new
 *    forwarded port:' static next to the rather taller `Remove'
 *    button.
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

void
ctrlposinit(ctrlpos * cp, HWND wnd, int leftborder, int rightborder,
           int topborder)
{
  RECT r, r2;
  cp->wnd = wnd;
  cp->font = SendMessage(wnd, WM_GETFONT, 0, 0);
  cp->ypos = topborder;
  GetClientRect(wnd, &r);
  r2.left = r2.top = 0;
  r2.right = 4;
  r2.bottom = 8;
  MapDialogRect(wnd, &r2);
  cp->dlu4inpix = r2.right;
  cp->width = (r.right * 4) / (r2.right) - 2 * GAPBETWEEN;
  cp->xoff = leftborder;
  cp->width -= leftborder + rightborder;
}

static HWND
doctl(ctrlpos * cp, RECT r, char *wclass, int wstyle, int exstyle, 
      char *wtext, int wid)
{
  HWND ctl;
 /*
  * Note nonstandard use of RECT. This is deliberate: by
  * transforming the width and height directly we arrange to
  * have all supposedly same-sized controls really same-sized.
  */

  r.left += cp->xoff;
  MapDialogRect(cp->wnd, &r);

 /*
  * We can pass in cp->wnd == null, to indicate a dry run
  * without creating any actual controls.
  */
  if (cp->wnd) {
    ctl =
      CreateWindowEx(exstyle, wclass, wtext, wstyle, r.left, r.top, r.right,
                     r.bottom, cp->wnd, (HMENU) wid, inst, null);
    SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(true, 0));

    if (!strcmp(wclass, "LISTBOX")) {
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
  doctl(cp, r, "BUTTON", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, 0,
        cp->boxtext ? cp->boxtext : "", cp->boxid);
  cp->ypos += GAPYBOX;
}

/*
 * A static line, followed by a full-width edit box.
 */
static void
editboxfw(ctrlpos * cp, int password, char *text, int staticid, int editid)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.right = cp->width;

  if (text) {
    r.top = cp->ypos;
    r.bottom = STATICHEIGHT;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, staticid);
    cp->ypos += STATICHEIGHT + GAPWITHIN;
  }
  r.top = cp->ypos;
  r.bottom = EDITHEIGHT;
  doctl(cp, r, "EDIT",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (password ?
                                                               ES_PASSWORD : 0),
        WS_EX_CLIENTEDGE, "", editid);
  cp->ypos += EDITHEIGHT + GAPBETWEEN;
}

/*
 * A static line, followed by a full-width combo box.
 */
static void
combobox(ctrlpos * cp, char *text, int staticid, int listid)
{
  RECT r;

  r.left = GAPBETWEEN;
  r.right = cp->width;

  if (text) {
    r.top = cp->ypos;
    r.bottom = STATICHEIGHT;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, staticid);
    cp->ypos += STATICHEIGHT + GAPWITHIN;
  }
  r.top = cp->ypos;
  r.bottom = COMBOHEIGHT * 10;
  doctl(cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", listid);
  cp->ypos += COMBOHEIGHT + GAPBETWEEN;
}

typedef struct {
  char *text;
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
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, id);
  }

  group = WS_GROUP;
  i = 0;
  for (j = 0; j < nbuttons; j++) {
    char *btext = buttons[j].text;
    int bid = buttons[j].id;

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
    doctl(cp, r, "BUTTON",
          BS_NOTIFY | BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          group, 0, btext, bid);
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
  doctl(cp, r, "BUTTON",
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
  doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, null, id);
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
  r.top = cp->ypos + (height - STATICHEIGHT) / 2;
  r.right = lwid;
  r.bottom = STATICHEIGHT;
  doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - PUSHBTNHEIGHT) / 2;
  r.right = rwid;
  r.bottom = PUSHBTNHEIGHT;
  doctl(cp, r, "BUTTON",
        BS_NOTIFY | WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
        btext, bid);

  cp->ypos += height + GAPBETWEEN;
}

/*
 * A simple push button.
 */
static void
button(ctrlpos * cp, char *btext, int bid, int defbtn)
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

  doctl(cp, r, "BUTTON",
        BS_NOTIFY | WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        (defbtn ? BS_DEFPUSHBUTTON : 0) | BS_PUSHBUTTON, 0, btext, bid);

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
  doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = EDITHEIGHT;
  doctl(cp, r, "EDIT",
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
 * A drop-down list box on the right hand side, with a static to
 * its left.
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
  doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = COMBOHEIGHT * 4;
  doctl(cp, r, "COMBOBOX",
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
  doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

  r.left = rpos;
  r.top = cp->ypos + (height - EDITHEIGHT) / 2;
  r.right = rwid;
  r.bottom = COMBOHEIGHT * 10;
  doctl(cp, r, "COMBOBOX",
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
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
    cp->ypos += STATICHEIGHT;
  }

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = COMBOHEIGHT * 4;
  doctl(cp, r, "COMBOBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
        CBS_HASSTRINGS, WS_EX_CLIENTEDGE, "", lid);
  cp->ypos += COMBOHEIGHT + GAPBETWEEN;
}

/*
 * A list box with a static labelling it.
 */
static void
listbox(ctrlpos * cp, char *stext, int sid, int lid, int lines)
{
  RECT r;

  if (stext != null) {
    r.left = GAPBETWEEN;
    r.top = cp->ypos;
    r.right = cp->width;
    r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
  }

  r.left = GAPBETWEEN;
  r.top = cp->ypos;
  r.right = cp->width;
  r.bottom = LISTHEIGHT + (lines - 1) * LISTINCREMENT;
  cp->ypos += r.bottom + GAPBETWEEN;
  doctl(cp, r, "LISTBOX",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
        LBS_HASSTRINGS | LBS_USETABSTOPS, WS_EX_CLIENTEDGE, "", lid);
}

/* ----------------------------------------------------------------------
 * Platform-specific side of portable dialog-box mechanism.
 */

/*
 * This function takes a string, escapes all the ampersands, and
 * places a single (unescaped) ampersand in front of the first
 * occurrence of the given shortcut character (which may be
 * NO_SHORTCUT).
 * 
 * Return value is a malloc'ed copy of the processed version of the
 * string.
 */
static char *
shortcut_escape(const char *text, char shortcut)
{
  char *ret;
  char const *p;
  char *q;

  if (!text)
    return null;        /* free won't choke on this */

  ret = newn(char, 2 * strlen(text) + 1);       /* size potentially doubles! */
  shortcut = tolower((uchar) shortcut);

  p = text;
  q = ret;
  while (*p) {
    if (shortcut != NO_SHORTCUT && tolower((uchar) * p) == shortcut) {
      *q++ = '&';
      shortcut = NO_SHORTCUT;   /* stop it happening twice */
    }
    else if (*p == '&') {
      *q++ = '&';
    }
    *q++ = *p++;
  }
  *q = '\0';
  return ret;
}

void
winctrl_add_shortcuts(dlgparam * dp, winctrl * c)
{
  for (size_t i = 0; i < lengthof(c->shortcuts); i++)
    if (c->shortcuts[i] != NO_SHORTCUT) {
      uchar s = tolower((uchar) c->shortcuts[i]);
      assert(!dp->shortcuts[s]);
      dp->shortcuts[s] = true;
    }
}

void
winctrl_rem_shortcuts(dlgparam * dp, winctrl * c)
{
  for (size_t i = 0; i < lengthof(c->shortcuts); i++)
    if (c->shortcuts[i] != NO_SHORTCUT) {
      uchar s = tolower((uchar) c->shortcuts[i]);
      assert(dp->shortcuts[s]);
      dp->shortcuts[s] = false;
    }
}

static int
winctrl_cmp_byctrl(void *av, void *bv)
{
  winctrl *a = (winctrl *) av;
  winctrl *b = (winctrl *) bv;
  if (a->ctrl < b->ctrl)
    return -1;
  else if (a->ctrl > b->ctrl)
    return +1;
  else
    return 0;
}
static int
winctrl_cmp_byid(void *av, void *bv)
{
  winctrl *a = (winctrl *) av;
  winctrl *b = (winctrl *) bv;
  if (a->base_id < b->base_id)
    return -1;
  else if (a->base_id > b->base_id)
    return +1;
  else
    return 0;
}
static int
winctrl_cmp_byctrl_find(void *av, void *bv)
{
  control *a = (control *) av;
  winctrl *b = (winctrl *) bv;
  if (a < b->ctrl)
    return -1;
  else if (a > b->ctrl)
    return +1;
  else
    return 0;
}
static int
winctrl_cmp_byid_find(void *av, void *bv)
{
  int *a = (int *) av;
  winctrl *b = (winctrl *) bv;
  if (*a < b->base_id)
    return -1;
  else if (*a >= b->base_id + b->num_ids)
    return +1;
  else
    return 0;
}

void
winctrl_init(winctrls * wc)
{
  wc->byctrl = newtree234(winctrl_cmp_byctrl);
  wc->byid = newtree234(winctrl_cmp_byid);
}

void
winctrl_cleanup(winctrls * wc)
{
  winctrl *c;

  while ((c = index234(wc->byid, 0)) != null) {
    winctrl_remove(wc, c);
    free(c->data);
    free(c);
  }

  freetree234(wc->byctrl);
  freetree234(wc->byid);
  wc->byctrl = wc->byid = null;
}

void
winctrl_add(winctrls * wc, winctrl * c)
{
  winctrl *ret;
  if (c->ctrl) {
    ret = add234(wc->byctrl, c);
    assert(ret == c);
  }
  ret = add234(wc->byid, c);
  assert(ret == c);
}

void
winctrl_remove(winctrls * wc, winctrl * c)
{
  winctrl *ret;
  ret = del234(wc->byctrl, c);
  ret = del234(wc->byid, c);
  assert(ret == c);
}

winctrl *
winctrl_findbyctrl(winctrls * wc, control *ctrl)
{
  return find234(wc->byctrl, ctrl, winctrl_cmp_byctrl_find);
}

winctrl *
winctrl_findbyid(winctrls * wc, int id)
{
  return find234(wc->byid, &id, winctrl_cmp_byid_find);
}

winctrl *
winctrl_findbyindex(winctrls * wc, int index)
{
  return index234(wc->byid, index);
}

void
winctrl_layout(dlgparam * dp, winctrls * wc, ctrlpos * cp,
               controlset * s, int *id)
{
  ctrlpos columns[16];
  int ncols, colstart, colspan;

  ctrlpos pos;

  char shortcuts[MAX_SHORTCUTS_PER_CTRL];
  int nshortcuts;
  int actual_base_id, base_id, num_ids;
  void *data;

  base_id = *id;

 /* Start a containing box, if we have a boxname. */
  if (s->boxname && *s->boxname) {
    winctrl *c = new(winctrl);
    c->ctrl = null;
    c->base_id = base_id;
    c->num_ids = 1;
    c->data = null;
    memset(c->shortcuts, NO_SHORTCUT, lengthof(c->shortcuts));
    winctrl_add(wc, c);
    beginbox(cp, s->boxtitle, base_id);
    base_id++;
  }

 /* Draw a title, if we have one. */
  if (!s->boxname && s->boxtitle) {
    winctrl *c = new(winctrl);
    c->ctrl = null;
    c->base_id = base_id;
    c->num_ids = 1;
    c->data = strdup(s->boxtitle);
    memset(c->shortcuts, NO_SHORTCUT, lengthof(c->shortcuts));
    winctrl_add(wc, c);
    paneltitle(cp, base_id);
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
    * `c' (a winctrl structure to receive details of the
    * dialog IDs). Or we'll have done a `continue', if it was
    * CTRL_COLUMNS and doesn't require any control creation at
    * all.
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

   /* And they all start off with no shortcuts registered. */
    memset(shortcuts, NO_SHORTCUT, lengthof(shortcuts));
    nshortcuts = 0;

   /* Almost all controls start at base_id. */
    actual_base_id = base_id;

   /*
    * Now we're ready to actually create the control, by
    * switching on its type.
    */
    switch (ctrl->type) {
      when CTRL_EDITBOX: {
        num_ids = 2;    /* static, edit */
        char *escaped = shortcut_escape(ctrl->label, ctrl->editbox.shortcut);
        shortcuts[nshortcuts++] = ctrl->editbox.shortcut;
        if (ctrl->editbox.percentwidth == 100) {
          if (ctrl->editbox.has_list)
            combobox(&pos, escaped, base_id, base_id + 1);
          else
            editboxfw(&pos, ctrl->editbox.password, escaped, base_id,
                      base_id + 1);
        }
        else {
          if (ctrl->editbox.has_list) {
            staticcombo(&pos, escaped, base_id, base_id + 1,
                        ctrl->editbox.percentwidth);
          }
          else {
            (ctrl->editbox.password ? staticpassedit : staticedit)
              (&pos, escaped, base_id, base_id + 1, ctrl->editbox.percentwidth);
          }
        }
        free(escaped);
      }
      when CTRL_RADIO: {
        num_ids = ctrl->radio.nbuttons + 1;     /* label as well */
        
        char *escaped = shortcut_escape(ctrl->label, ctrl->radio.shortcut);
        shortcuts[nshortcuts++] = ctrl->radio.shortcut;

        radio *buttons = newn(radio, ctrl->radio.nbuttons);

        for (int i = 0; i < ctrl->radio.nbuttons; i++) {
          buttons[i].text =
            shortcut_escape(ctrl->radio.buttons[i],
                            (char) (ctrl->radio.shortcuts ? ctrl->radio.
                                    shortcuts[i] : NO_SHORTCUT));
          buttons[i].id = base_id + 1 + i;
          if (ctrl->radio.shortcuts) {
            assert(nshortcuts < MAX_SHORTCUTS_PER_CTRL);
            shortcuts[nshortcuts++] = ctrl->radio.shortcuts[i];
          }
        }

        radioline_common(&pos, escaped, base_id, ctrl->radio.ncolumns,
                         buttons, ctrl->radio.nbuttons);

        for (int i = 0; i < ctrl->radio.nbuttons; i++)
          free(buttons[i].text);
        free(buttons);
        free(escaped);
      }
      when CTRL_CHECKBOX: {
        num_ids = 1;
        char *escaped = shortcut_escape(ctrl->label, ctrl->checkbox.shortcut);
        shortcuts[nshortcuts++] = ctrl->checkbox.shortcut;
        checkbox(&pos, escaped, base_id);
        free(escaped);
      }
      when CTRL_BUTTON: {
        char *escaped = shortcut_escape(ctrl->label, ctrl->button.shortcut);
        shortcuts[nshortcuts++] = ctrl->button.shortcut;
        if (ctrl->button.iscancel)
          actual_base_id = IDCANCEL;
        num_ids = 1;
        button(&pos, escaped, actual_base_id, ctrl->button.isdefault);
        free(escaped);
      }
      when CTRL_LISTBOX: {
        num_ids = 2;
        char *escaped = shortcut_escape(ctrl->label, ctrl->listbox.shortcut);
        shortcuts[nshortcuts++] = ctrl->listbox.shortcut;
        if (ctrl->listbox.height == 0) {
         /* Drop-down list. */
          if (ctrl->listbox.percentwidth == 100)
            staticddlbig(&pos, escaped, base_id, base_id + 1);
          else
            staticddl(&pos, escaped, base_id, base_id + 1,
                      ctrl->listbox.percentwidth);
        }
        else {
         /* Ordinary list. */
          listbox(&pos, escaped, base_id, base_id + 1, ctrl->listbox.height);
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
          int *tabarray = newn(int, ctrl->listbox.ncols - 1);
          int percent = 0;
          for (int i = 0; i < ctrl->listbox.ncols - 1; i++) {
            percent += ctrl->listbox.percentages[i];
            tabarray[i] = width * percent / 10000;
          }
          SendDlgItemMessage(cp->wnd, base_id + 1, LB_SETTABSTOPS,
                             ctrl->listbox.ncols - 1, (LPARAM) tabarray);
          free(tabarray);
        }
        free(escaped);
      }
      when CTRL_FONTSELECT: {
        num_ids = 3;
        char *escaped = shortcut_escape(ctrl->label, ctrl->fontselect.shortcut);
        shortcuts[nshortcuts++] = ctrl->fontselect.shortcut;
        //statictext(&pos, escaped, 1, base_id);
        staticbtn(&pos, "", base_id + 1, "&Font...", base_id + 2);
        free(escaped);
        data = new(font_spec);
      }
      otherwise:
        assert(!"Can't happen");
        num_ids = 0;    /* placate gcc */
        break;
    }

   /*
    * Create a `winctrl' for this control, and advance
    * the dialog ID counter, if it's actually been created
    */
    if (pos.wnd) {
      winctrl *c = new(winctrl);

      c->ctrl = ctrl;
      c->base_id = actual_base_id;
      c->num_ids = num_ids;
      c->data = data;
      memcpy(c->shortcuts, shortcuts, sizeof (shortcuts));
      winctrl_add(wc, c);
      winctrl_add_shortcuts(dp, c);
      if (actual_base_id == base_id)
        base_id += num_ids;
    }

    if (colstart >= 0) {
     /*
      * Update the ypos in all columns crossed by this
      * control.
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

  if (s->boxname && *s->boxname)
    endbox(cp);
}

static void
winctrl_set_focus(control *ctrl, dlgparam * dp, int has_focus)
{
  if (has_focus) {
    if (dp->focused)
      dp->lastfocused = dp->focused;
    dp->focused = ctrl;
  }
  else if (!has_focus && dp->focused == ctrl) {
    dp->lastfocused = dp->focused;
    dp->focused = null;
  }
}

static void
select_font(winctrl *c, dlgparam *dp)
{
  font_spec fs = *(font_spec *) c->data;
  HDC dc = GetDC(0);
  LOGFONT lf;
  lf.lfHeight = -MulDiv(fs.size, GetDeviceCaps(dc, LOGPIXELSY), 72);
  ReleaseDC(0, dc);
  lf.lfWidth = lf.lfEscapement = lf.lfOrientation = 0;
  lf.lfItalic = lf.lfUnderline = lf.lfStrikeOut = 0;
  lf.lfWeight = (fs.isbold ? FW_BOLD : 0);
  lf.lfCharSet = fs.charset;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = DEFAULT_QUALITY;
  lf.lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
  strncpy(lf.lfFaceName, fs.name, sizeof (lf.lfFaceName) - 1);
  lf.lfFaceName[sizeof (lf.lfFaceName) - 1] = '\0';

  CHOOSEFONT cf;
  cf.lStructSize = sizeof (cf);
  cf.hwndOwner = dp->wnd;
  cf.lpLogFont = &lf;
  cf.Flags =
    CF_FIXEDPITCHONLY | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT |
    CF_SCREENFONTS;

  if (ChooseFont(&cf)) {
    strncpy(fs.name, lf.lfFaceName, sizeof (fs.name) - 1);
    fs.name[sizeof (fs.name) - 1] = '\0';
    fs.isbold = (lf.lfWeight == FW_BOLD);
    fs.charset = lf.lfCharSet;
    fs.size = cf.iPointSize / 10;
    dlg_fontsel_set(c->ctrl, dp, &fs);
    c->ctrl->handler(c->ctrl, dp, dp->data, EVENT_VALCHANGE);
  }
}

/*
 * The dialog-box procedure calls this function to handle Windows
 * messages on a control we manage.
 */
int
winctrl_handle_command(dlgparam * dp, UINT msg, WPARAM wParam, LPARAM lParam)
{
  winctrl *c;
  control *ctrl;
  int i, id, ret;

 /*
  * Look up the control ID in our data.
  */
  c = null;
  for (i = 0; i < dp->nctrltrees; i++) {
    c = winctrl_findbyid(dp->controltrees[i], LOWORD(wParam));
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

    GetTextExtentPoint32(dc, (char *) c->data, strlen((char *) c->data), &s);
    DrawEdge(dc, &r, EDGE_ETCHED, BF_ADJUST | BF_RECT);
    TextOut(dc, r.left + (r.right - r.left - s.cx) / 2,
            r.top + (r.bottom - r.top - s.cy) / 2, (char *) c->data,
            strlen((char *) c->data));

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
  * subsequent code can test dp->coloursel_wanted().
  */
  ret = 0;
  dp->coloursel_wanted = false;

 /*
  * Now switch on the control type and the message.
  */
  if (msg == WM_COMMAND) {
    WORD note = HIWORD(wParam);
    switch (ctrl->type) {
      when CTRL_RADIO:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, dp, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
           /*
            * We sometimes get spurious BN_CLICKED messages for the
            * radio button that is just about to _lose_ selection, if
            * we're switching using the arrow keys. Therefore we
            * double-check that the button in wParam is actually
            * checked before generating an event.
            */
            if (IsDlgButtonChecked(dp->wnd, LOWORD(wParam)))
              ctrl->handler(ctrl, dp, dp->data, EVENT_VALCHANGE);
        }
      when CTRL_CHECKBOX:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, dp, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
            ctrl->handler(ctrl, dp, dp->data, EVENT_VALCHANGE);
        }
      when CTRL_BUTTON:
        switch (note) {
          when BN_SETFOCUS or BN_KILLFOCUS:
            winctrl_set_focus(ctrl, dp, note == BN_SETFOCUS);
          when BN_CLICKED or BN_DOUBLECLICKED:
            ctrl->handler(ctrl, dp, dp->data, EVENT_ACTION);
        }
      when CTRL_FONTSELECT:
        if (id == 2) {
          switch (note) {
            when BN_SETFOCUS or BN_KILLFOCUS:
              winctrl_set_focus(ctrl, dp, note == BN_SETFOCUS);
            when BN_CLICKED or BN_DOUBLECLICKED:
              select_font(c, dp);
          }
        }
      when CTRL_LISTBOX:
        if (ctrl->listbox.height != 0 &&
            (note == LBN_SETFOCUS || note == LBN_KILLFOCUS))
          winctrl_set_focus(ctrl, dp, note == LBN_SETFOCUS);
        else if (ctrl->listbox.height == 0 &&
            (note == CBN_SETFOCUS || note == CBN_KILLFOCUS))
          winctrl_set_focus(ctrl, dp, note == CBN_SETFOCUS);
        else if (id >= 2 && (note == BN_SETFOCUS || note == BN_KILLFOCUS))
          winctrl_set_focus(ctrl, dp, note == BN_SETFOCUS);
        else if (note == LBN_DBLCLK) {
          SetCapture(dp->wnd);
          ctrl->handler(ctrl, dp, dp->data, EVENT_ACTION);
        }
        else if (note == LBN_SELCHANGE)
          ctrl->handler(ctrl, dp, dp->data, EVENT_SELCHANGE);
      when CTRL_EDITBOX:
        if (ctrl->editbox.has_list) {
          switch (note) {
            when CBN_SETFOCUS:
              winctrl_set_focus(ctrl, dp, true);
            when CBN_KILLFOCUS:
              winctrl_set_focus(ctrl, dp, false);
              ctrl->handler(ctrl, dp, dp->data, EVENT_UNFOCUS);
            when CBN_SELCHANGE: {
              int index = SendDlgItemMessage(
                            dp->wnd, c->base_id + 1, CB_GETCURSEL, 0, 0);
              int len = SendDlgItemMessage(
                          dp->wnd, c->base_id + 1, CB_GETLBTEXTLEN, index, 0);
              char text[len + 1];
              SendDlgItemMessage(
                dp->wnd, c->base_id + 1, CB_GETLBTEXT, index, (LPARAM) text);
              SetDlgItemText(dp->wnd, c->base_id + 1, text);
              ctrl->handler(ctrl, dp, dp->data, EVENT_SELCHANGE);
            }
            when CBN_EDITCHANGE:
              ctrl->handler(ctrl, dp, dp->data, EVENT_VALCHANGE);
          }
        }
        else {
          switch (note) {
            when EN_SETFOCUS:
              winctrl_set_focus(ctrl, dp, true);
            when EN_KILLFOCUS:
              winctrl_set_focus(ctrl, dp, false);
              ctrl->handler(ctrl, dp, dp->data, EVENT_UNFOCUS);
            when EN_CHANGE:
              ctrl->handler(ctrl, dp, dp->data, EVENT_VALCHANGE);
          }
        }
    }
  }

 /*
  * If the above event handler has asked for a colour selector,
  * now is the time to generate one.
  */
  if (dp->coloursel_wanted) {
    static CHOOSECOLOR cc;
    static DWORD custom[16] = { 0 };    /* zero initialisers */
    cc.lStructSize = sizeof (cc);
    cc.hwndOwner = dp->wnd;
    cc.hInstance = (HWND) inst;
    cc.lpCustColors = custom;
    cc.rgbResult = dp->coloursel_result;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    dp->coloursel_ok = ChooseColor(&cc);
    dp->coloursel_result = cc.rgbResult;
    ctrl->handler(ctrl, dp, dp->data, EVENT_CALLBACK);
  }

  return ret;
}

/*
 * Now the various functions that the platform-independent
 * mechanism can call to access the dialog box entries.
 */

static winctrl *
dlg_findbyctrl(dlgparam * dp, control *ctrl)
{
  int i;

  for (i = 0; i < dp->nctrltrees; i++) {
    winctrl *c = winctrl_findbyctrl(dp->controltrees[i], ctrl);
    if (c)
      return c;
  }
  return null;
}

void
dlg_radiobutton_set(control *ctrl, void *dlg, int whichbutton)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_RADIO);
  CheckRadioButton(dp->wnd, c->base_id + 1,
                   c->base_id + c->ctrl->radio.nbuttons,
                   c->base_id + 1 + whichbutton);
}

int
dlg_radiobutton_get(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  int i;
  assert(c && c->ctrl->type == CTRL_RADIO);
  for (i = 0; i < c->ctrl->radio.nbuttons; i++)
    if (IsDlgButtonChecked(dp->wnd, c->base_id + 1 + i))
      return i;
  assert(!"No radio button was checked?!");
  return 0;
}

void
dlg_checkbox_set(control *ctrl, void *dlg, int checked)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_CHECKBOX);
  CheckDlgButton(dp->wnd, c->base_id, (checked != 0));
}

int
dlg_checkbox_get(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_CHECKBOX);
  return 0 != IsDlgButtonChecked(dp->wnd, c->base_id);
}

void
dlg_editbox_set(control *ctrl, void *dlg, char const *text)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_EDITBOX);
  SetDlgItemText(dp->wnd, c->base_id + 1, text);
}

void
dlg_editbox_get(control *ctrl, void *dlg, char *buffer, int length)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_EDITBOX);
  GetDlgItemText(dp->wnd, c->base_id + 1, buffer, length);
  buffer[length - 1] = '\0';
}

/* The `listbox' functions also apply to combo boxes. */
void
dlg_listbox_clear(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  int msg;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  msg = (c->ctrl->type == CTRL_LISTBOX &&
         c->ctrl->listbox.height != 0 ? LB_RESETCONTENT : CB_RESETCONTENT);
  SendDlgItemMessage(dp->wnd, c->base_id + 1, msg, 0, 0);
}

void
dlg_listbox_add(control *ctrl, void *dlg, char const *text)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  int msg;
  assert(c &&
         (c->ctrl->type == CTRL_LISTBOX ||
          (c->ctrl->type == CTRL_EDITBOX &&
           c->ctrl->editbox.has_list)));
  msg = (c->ctrl->type == CTRL_LISTBOX &&
         c->ctrl->listbox.height != 0 ? LB_ADDSTRING : CB_ADDSTRING);
  SendDlgItemMessage(dp->wnd, c->base_id + 1, msg, 0, (LPARAM) text);
}

void
dlg_label_change(control *ctrl, void *dlg, char const *text)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  char *escaped = null;
  int id = -1;

  assert(c);
  switch (c->ctrl->type) {
    when CTRL_EDITBOX:
      escaped = shortcut_escape(text, c->ctrl->editbox.shortcut);
      id = c->base_id;
    when CTRL_RADIO:
      escaped = shortcut_escape(text, c->ctrl->radio.shortcut);
      id = c->base_id;
    when CTRL_CHECKBOX:
      escaped = shortcut_escape(text, ctrl->checkbox.shortcut);
      id = c->base_id;
    when CTRL_BUTTON:
      escaped = shortcut_escape(text, ctrl->button.shortcut);
      id = c->base_id;
    when CTRL_LISTBOX:
      escaped = shortcut_escape(text, ctrl->listbox.shortcut);
      id = c->base_id;
    when CTRL_FONTSELECT:
      escaped = shortcut_escape(text, ctrl->fontselect.shortcut);
      id = c->base_id;
    otherwise:
      assert(!"Can't happen");
  }
  if (escaped) {
    SetDlgItemText(dp->wnd, id, escaped);
    free(escaped);
  }
}

void
dlg_fontsel_set(control *ctrl, void *dlg, font_spec *fs)
{
  char *buf, *boldstr;
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_FONTSELECT);

  *(font_spec *) c->data = *fs;   /* structure copy */

  boldstr = fs->isbold ? "bold, " : "";
  if (!fs->size)
    asprintf(&buf, "%s, %sdefault height", fs->name, boldstr);
  else
    asprintf(&buf, "%s, %s%d-%s", fs->name, boldstr, abs(fs->size),
             fs->size < 0 ? "pixel" : "point");
  SetDlgItemText(dp->wnd, c->base_id + 1, buf);
  free(buf);
}

void
dlg_fontsel_get(control *ctrl, void *dlg, font_spec *fs)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->type == CTRL_FONTSELECT);
  *fs = *(font_spec *) c->data;  /* structure copy */
}

/*
 * Bracketing a large set of updates in these two functions will
 * cause the front end (if possible) to delay updating the screen
 * until it's all complete, thus avoiding flicker.
 */
void
dlg_update_start(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  if (c && c->ctrl->type == CTRL_LISTBOX) {
    SendDlgItemMessage(dp->wnd, c->base_id + 1, WM_SETREDRAW, false, 0);
  }
}

void
dlg_update_done(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  if (c && c->ctrl->type == CTRL_LISTBOX) {
    HWND hw = GetDlgItem(dp->wnd, c->base_id + 1);
    SendMessage(hw, WM_SETREDRAW, true, 0);
    InvalidateRect(hw, null, true);
  }
}

#if 0 // Unused
void
dlg_enable(control *ctrl, void *dlg, bool enable)
{
  dlgparam *dp = (dlgparam *) dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  EnableWindow(GetDlgItem(dp->wnd, c->base_id + 1), enable);
}
#endif

void
dlg_set_focus(control *ctrl, void *dlg)
{
  dlgparam *dp = dlg;
  winctrl *c = dlg_findbyctrl(dp, ctrl);
  int id;
  switch (ctrl->type) {
    when CTRL_EDITBOX or CTRL_LISTBOX: id = c->base_id + 1;
    when CTRL_FONTSELECT: id = c->base_id + 2;
    when CTRL_RADIO:
      id = c->base_id + ctrl->radio.nbuttons;
      while (id > 1 && IsDlgButtonChecked(dp->wnd, id))
        --id;
     /*
      * In the theoretically-unlikely case that no button was
      * selected, id should come out of this as 1, which is a
      * reasonable enough choice.
      */
    otherwise: id = c->base_id;
  }
  SetFocus(GetDlgItem(dp->wnd, id));
}

/*
 * This function signals to the front end that the dialog's
 * processing is completed, and passes an integer value (typically
 * a success status).
 */
void
dlg_end(void *dlg)
{
  ((dlgparam *) dlg)->ended = true;
}

void
dlg_refresh(control *ctrl, void *dlg)
{
  dlgparam *dp = (dlgparam *) dlg;
  int i, j;
  winctrl *c;

  if (!ctrl) {
   /*
    * Send EVENT_REFRESH to absolutely everything.
    */
    for (j = 0; j < dp->nctrltrees; j++) {
      for (i = 0; (c = winctrl_findbyindex(dp->controltrees[j], i)) != null;
           i++) {
        if (c->ctrl && c->ctrl->handler != null)
          c->ctrl->handler(c->ctrl, dp, dp->data, EVENT_REFRESH);
      }
    }
  }
  else {
   /*
    * Send EVENT_REFRESH to a specific control.
    */
    if (ctrl->handler != null)
      ctrl->handler(ctrl, dp, dp->data, EVENT_REFRESH);
  }
}

void
dlg_coloursel_start(void *dlg, colour c)
{
  dlgparam *dp = (dlgparam *) dlg;
  dp->coloursel_wanted = true;
  dp->coloursel_result = c;
}

int
dlg_coloursel_results(void *dlg, colour *res_p)
{
  dlgparam *dp = (dlgparam *) dlg;
  bool ok = dp->coloursel_ok ;
  if (ok)
    *res_p = dp->coloursel_result;
  return ok;
}

typedef struct {
  control *ctrl;
  void *data;
  int needs_free;
} perctrl_privdata;

static int
perctrl_privdata_cmp(void *av, void *bv)
{
  perctrl_privdata *a = (perctrl_privdata *) av;
  perctrl_privdata *b = (perctrl_privdata *) bv;
  if (a->ctrl < b->ctrl)
    return -1;
  else if (a->ctrl > b->ctrl)
    return +1;
  return 0;
}

void
dp_init(dlgparam * dp)
{
  dp->nctrltrees = 0;
  dp->data = null;
  dp->ended = false;
  dp->focused = dp->lastfocused = null;
  memset(dp->shortcuts, 0, sizeof (dp->shortcuts));
  dp->wnd = null;
  dp->wintitle = null;
  dp->privdata = newtree234(perctrl_privdata_cmp);
}

void
dp_add_tree(dlgparam * dp, winctrls * wc)
{
  assert(dp->nctrltrees < (int) lengthof(dp->controltrees));
  dp->controltrees[dp->nctrltrees++] = wc;
}

void
dp_cleanup(dlgparam * dp)
{
  perctrl_privdata *p;

  if (dp->privdata) {
    while ((p = index234(dp->privdata, 0)) != null) {
      del234(dp->privdata, p);
      if (p->needs_free)
        free(p->data);
      free(p);
    }
    freetree234(dp->privdata);
    dp->privdata = null;
  }
  free(dp->wintitle);
}
