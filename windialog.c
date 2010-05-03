// windialog.c (part of mintty)
// Copyright 2008-10 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "ctrls.h"
#include "config.h"
#include "winctrls.h"
#include "winids.h"
#include "appinfo.h"

#include <commctrl.h>

void setup_config_box(controlbox *);

/*
 * windlg.c - Dialogs, including the configuration dialog.
 */

/*
 * These are the various bits of data required to handle the
 * portable-dialog stuff in the config box. Having them at file
 * scope in here isn't too bad a place to put them; if we were ever
 * to need more than one config box per process we could always
 * shift them to a per-config-box structure stored in GWL_USERDATA.
 */
static controlbox *ctrlbox;
/*
 * ctrls_base holds the OK and Cancel buttons: the controls which
 * are present in all dialog panels. ctrls_panel holds the ones
 * which change from panel to panel.
 */
static winctrls ctrls_base, ctrls_panel;

windlg dlg;

#define PRINTER_DISABLED_STRING "None (printing disabled)"

static void
force_normal(HWND wnd)
{
  static int recurse = 0;

  WINDOWPLACEMENT wp;

  if (recurse)
    return;
  recurse = 1;

  wp.length = sizeof (wp);
  if (GetWindowPlacement(wnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
    wp.showCmd = SW_SHOWNORMAL;
    SetWindowPlacement(wnd, &wp);
  }
  recurse = 0;
}
enum {
  IDCX_TVSTATIC = 1001,
  IDCX_TREEVIEW,
  IDCX_STDBASE,
  IDCX_PANELBASE = IDCX_STDBASE + 32
};

typedef struct {
  HWND treeview;
  HTREEITEM lastat[4];
} treeview_faff;

static HTREEITEM
treeview_insert(treeview_faff * faff, int level, char *text, char *path)
{
  TVINSERTSTRUCT ins;
  int i;
  HTREEITEM newitem;
  ins.hParent = (level > 0 ? faff->lastat[level - 1] : TVI_ROOT);
  ins.hInsertAfter = faff->lastat[level];
  ins.item.mask = TVIF_TEXT | TVIF_PARAM;
  ins.item.pszText = text;
  ins.item.cchTextMax = strlen(text) + 1;
  ins.item.lParam = (LPARAM) path;
  newitem = TreeView_InsertItem(faff->treeview, &ins);
  if (level > 0)
    TreeView_Expand(faff->treeview, faff->lastat[level - 1],
                    (level > 1 ? TVE_COLLAPSE : TVE_EXPAND));
  faff->lastat[level] = newitem;
  for (i = level + 1; i < 4; i++)
    faff->lastat[i] = null;
  return newitem;
}

/*
 * Create the panelfuls of controls in the configuration box.
 */
static void
create_controls(HWND wnd, char *path)
{
  ctrlpos cp;
  int index;
  int base_id;
  winctrls *wc;

  if (!path[0]) {
   /*
    * Here we must create the basic standard controls.
    */
    ctrlposinit(&cp, wnd, 3, 3, 135);
    wc = &ctrls_base;
    base_id = IDCX_STDBASE;
  }
  else {
   /*
    * Otherwise, we're creating the controls for a particular
    * panel.
    */
    ctrlposinit(&cp, wnd, 69, 3, 3);
    wc = &ctrls_panel;
    base_id = IDCX_PANELBASE;
  }

  for (index = -1; (index = ctrl_find_path(ctrlbox, path, index)) >= 0;) {
    controlset *s = ctrlbox->ctrlsets[index];
    winctrl_layout(wc, &cp, s, &base_id);
  }
}

/*
 * This function is the configuration box.
 * (Being a dialog procedure, in general it returns 0 if the default
 * dialog processing should be performed, and 1 if it should not.)
 */
static int CALLBACK
config_dialog_proc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    when WM_INITDIALOG: {
      ctrlbox = ctrl_new_box();
      setup_config_box(ctrlbox);
      windlg_init();
      winctrl_init(&ctrls_base);
      winctrl_init(&ctrls_panel);
      windlg_add_tree(&ctrls_base);
      windlg_add_tree(&ctrls_panel);
      asprintf(&dlg.wintitle, "Options");
      new_cfg = cfg;
      dlg.data = &new_cfg;

      RECT r;
      GetWindowRect(GetParent(wnd), &r);
      dlg.wnd = wnd;
      create_controls(wnd, "");        /* Open and Cancel buttons etc */
      SetWindowText(wnd, dlg.wintitle);
      SetWindowLongPtr(wnd, GWLP_USERDATA, 0);
      SendMessage(wnd, WM_SETICON, (WPARAM) ICON_BIG,
                  (LPARAM) LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON)));

     /*
      * Create the tree view.
      */
      WPARAM font = SendMessage(wnd, WM_GETFONT, 0, 0);

      r.left = 3;
      r.right = r.left + 64;
      r.top = 3;
      r.bottom = r.top + 126;
      MapDialogRect(wnd, &r);
      HWND treeview =
        CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, "",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
                       TVS_DISABLEDRAGDROP | TVS_HASBUTTONS | TVS_LINESATROOT
                       | TVS_SHOWSELALWAYS, r.left, r.top, r.right - r.left,
                       r.bottom - r.top, wnd, (HMENU) IDCX_TREEVIEW, inst,
                       null);
      font = SendMessage(wnd, WM_GETFONT, 0, 0);
      SendMessage(treeview, WM_SETFONT, font, MAKELPARAM(true, 0));
      treeview_faff tvfaff;
      tvfaff.treeview = treeview;
      memset(tvfaff.lastat, 0, sizeof (tvfaff.lastat));

     /*
      * Set up the tree view contents.
      */
      HTREEITEM hfirst = null;
      char *path = null;

      for (int i = 0; i < ctrlbox->nctrlsets; i++) {
        controlset *s = ctrlbox->ctrlsets[i];
        HTREEITEM item;
        int j;
        char *c;

        if (!s->pathname[0])
          continue;
        j = path ? ctrl_path_compare(s->pathname, path) : 0;
        if (j == INT_MAX)
          continue;   /* same path, nothing to add to tree */

       /*
        * We expect never to find an implicit path
        * component. For example, we expect never to see
        * A/B/C followed by A/D/E, because that would
        * _implicitly_ create A/D. All our path prefixes
        * are expected to contain actual controls and be
        * selectable in the treeview; so we would expect
        * to see A/D _explicitly_ before encountering
        * A/D/E.
        */
        assert(j == ctrl_path_elements(s->pathname) - 1);

        c = strrchr(s->pathname, '/');
        if (!c)
          c = s->pathname;
        else
          c++;

        item = treeview_insert(&tvfaff, j, c, s->pathname);
        if (!hfirst)
          hfirst = item;

        path = s->pathname;

       /*
        * Put the treeview selection on to the Session panel.
        * This should also cause creation of the relevant
        * controls.
        */
        TreeView_SelectItem(treeview, hfirst);
      }
     /*
      * Set focus into the first available control.
      */
      for (winctrl *c = ctrls_panel.first; c; c = c->next) {
        if (c->ctrl) {
          dlg_set_focus(c->ctrl);
          break;
        }
      }
    }

    when WM_DESTROY:
      winctrl_cleanup(&ctrls_base);
      winctrl_cleanup(&ctrls_panel);
      ctrl_free_box(ctrlbox);
      windlg_cleanup();
      config_wnd = 0;

    when WM_NOTIFY: {
      if (LOWORD(wParam) == IDCX_TREEVIEW &&
          ((LPNMHDR) lParam)->code == TVN_SELCHANGED) {
        HTREEITEM i = TreeView_GetSelection(((LPNMHDR) lParam)->hwndFrom);
        TVITEM item;
        char buffer[64];

        SendMessage(wnd, WM_SETREDRAW, false, 0);

        item.hItem = i;
        item.pszText = buffer;
        item.cchTextMax = sizeof (buffer);
        item.mask = TVIF_TEXT | TVIF_PARAM;
        TreeView_GetItem(((LPNMHDR) lParam)->hwndFrom, &item);

       /* Destroy all controls in the currently visible panel. */
        for (winctrl *c = ctrls_panel.first; c; c = c->next) {
          for (int k = 0; k < c->num_ids; k++) {
            HWND item = GetDlgItem(wnd, c->base_id + k);
            if (item)
              DestroyWindow(item);
          }
          winctrl_rem_shortcuts(c);
        }
        winctrl_cleanup(&ctrls_panel);
        
        create_controls(wnd, (char *) item.lParam);
        dlg_refresh(null); /* set up control values */
        SendMessage(wnd, WM_SETREDRAW, true, 0);
        InvalidateRect(wnd, null, true);
        SetFocus(((LPNMHDR) lParam)->hwndFrom); /* ensure focus stays */
      }
    }
    
    when WM_CLOSE:
      DestroyWindow(wnd);

     /* Grrr Explorer will maximize Dialogs! */
    when WM_SIZE:
      if (wParam == SIZE_MAXIMIZED)
        force_normal(wnd);

    when WM_COMMAND or WM_DRAWITEM: { 
      int ret = winctrl_handle_command(msg, wParam, lParam);
      if (dlg.ended)
        DestroyWindow(wnd);
      return ret;
    }
  }
  return 0;
}

HWND config_wnd;

void
win_open_config(void)
{
  if (config_wnd)
    return;
  
  InitCommonControls();

  RegisterClass(&(WNDCLASS){
    .style = CS_DBLCLKS | CS_SAVEBITS,
    .lpfnWndProc = DefDlgProc,
    .cbClsExtra = 0,
    .cbWndExtra = DLGWINDOWEXTRA + 2 * sizeof (LONG_PTR),
    .hInstance = inst,
    .hIcon = null,
    .hCursor = LoadCursor(null, IDC_ARROW),
    .hbrBackground = (HBRUSH) (COLOR_BACKGROUND + 1),
    .lpszMenuName = null,
    .lpszClassName = "ConfigBox"
  });
  config_wnd = CreateDialog(inst, MAKEINTRESOURCE(IDD_MAINBOX),
                            wnd, config_dialog_proc);
}  
