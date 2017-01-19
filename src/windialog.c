// windialog.c (part of mintty)
// Copyright 2008-11 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "winpriv.h"

#include "ctrls.h"
#include "winctrls.h"
#include "winids.h"
#include "res.h"
#include "appinfo.h"

#include "charset.h"  // nonascii, cs__utftowcs
extern void setup_config_box(controlbox *);

#include <commctrl.h>

# define debug_dialog_crash

#ifdef debug_dialog_crash
#include <signal.h>
#endif


/*
 * windlg.c - Dialogs, including the configuration dialog.

   To make the TreeView widget work with Unicode support, 
   it is particularly essential to use message TVM_INSERTITEMW 
   to insert a TVINSERTSTRUCTW.

   To document a minimum set of Unicode-enabled API usage as could be 
   identified, some calls below are explicitly maintained in "ANSI" mode:
     RegisterClassA			would work for the TreeView
     RegisterClassW			needs "W" for title if UNICODE
     RegisterClassW with DefDlgProcW	needed for proper window title
     CreateDialogW			must be "W" if UNICODE is defined
     CreateWindowExA			works
   The TreeView_ macros are implicitly mapped to either "A" or "W", 
   so to use TreeView_InsertItem in either mode, it needs to be expanded 
   to SendMessageA/SendMessageW.
 */

/*
 * These are the various bits of data required to handle the
 * portable-dialog stuff in the config box.
 */
static controlbox *ctrlbox;
/*
 * ctrls_base holds the OK and Cancel buttons: the controls which
 * are present in all dialog panels. ctrls_panel holds the ones
 * which change from panel to panel.
 */
static winctrls ctrls_base, ctrls_panel;

windlg dlg;
wstring dragndrop;

static int dialog_height;  // dummy

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
// text will be the label of an Options dialog treeview item;
// it is passed in here as the basename of path

  HTREEITEM newitem;

  if (nonascii(path)) {
    wchar * utext = cs__utftowcs(text);
    TVINSERTSTRUCTW ins;
    ins.hParent = (level > 0 ? faff->lastat[level - 1] : TVI_ROOT);
    ins.hInsertAfter = faff->lastat[level];
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = utext;
    //ins.item.cchTextMax = wcslen(utext) + 1;  // ignored when setting
    ins.item.lParam = (LPARAM) path;
    // It is essential to also use TVM_INSERTITEMW here!
    newitem = (HTREEITEM)SendMessageW(faff->treeview, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    //TreeView_SetUnicodeFormat((HWND)newitem, TRUE);  // does not work
    free(utext);
  }
  else {
    TVINSERTSTRUCTA ins;
    ins.hParent = (level > 0 ? faff->lastat[level - 1] : TVI_ROOT);
    ins.hInsertAfter = faff->lastat[level];
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = text;
    //ins.item.cchTextMax = strlen(text) + 1;  // ignored when setting
    ins.item.lParam = (LPARAM) path;
    //newitem = TreeView_InsertItem(faff->treeview, &ins);
    newitem = (HTREEITEM)SendMessageA(faff->treeview, TVM_INSERTITEMA, 0, (LPARAM)&ins);
  }

  if (level > 0)
    TreeView_Expand(faff->treeview, faff->lastat[level - 1],
                    (level > 1 ? TVE_COLLAPSE : TVE_EXPAND));
  faff->lastat[level] = newitem;
  for (int i = level + 1; i < 4; i++)
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
    ctrlposinit(&cp, wnd, 3, 3, DIALOG_HEIGHT - 17);
    wc = &ctrls_base;
    base_id = IDCX_STDBASE;
  }
  else {
   /*
    * Otherwise, we're creating the controls for a particular panel.
    */
    ctrlposinit(&cp, wnd, 69, 3, 3);
    wc = &ctrls_panel;
    base_id = IDCX_PANELBASE;
  }

#ifdef debug_layout
  printf("create_controls (%s)\n", path);
#endif
  for (index = -1; (index = ctrl_find_path(ctrlbox, path, index)) >= 0;) {
    controlset *s = ctrlbox->ctrlsets[index];
    winctrl_layout(wc, &cp, s, &base_id);
  }
}

static void
determine_geometry(HWND wnd)
{
  // determine height in dialog box coordinates
  // as was configured in res.rc (IDD_MAINBOX) and applied magically
  RECT r;
  GetClientRect(wnd, &r);

  RECT normr;
  normr.left = 0;
  normr.top = 0;
  normr.right = 100;
  normr.bottom = 100;
  MapDialogRect(config_wnd, &normr);

  dialog_height = 100 * (r.bottom - r.top) / normr.bottom;
}

#ifdef debug_dialog_crash

static char * debugopt = 0;
static char * debugtag = "none";

static void
sigsegv(int sig)
{
  signal(sig, SIG_DFL);
  printf("catch %d: %s\n", sig, debugtag);
  fflush(stdout);
  MessageBoxA(0, debugtag, "Critical Error", MB_ICONSTOP);
}

inline static void
crashtest()
{
  char * x0 = 0;
  *x0 = 'x';
}

static void
debug(char *tag)
{
  if (!debugopt) {
    debugopt = getenv("MINTTY_DEBUG");
    if (!debugopt)
      debugopt = "";
  }

  debugtag = tag;

  if (debugopt && *debugopt)
    printf("%s\n", tag);
}

#else
# define debug(tag)	
#endif

#define dont_debug_messages

/*
 * This function is the configuration box.
 * (Being a dialog procedure, in general it returns 0 if the default
 * dialog processing should be performed, and 1 if it should not.)
 */
static INT_PTR CALLBACK
config_dialog_proc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
#ifdef debug_messages
#include <time.h>
  static struct {
  uint wm_;
  char * wm_name;
  } wm_names[] = {
#  include "_wm.t"
  };
  char * wm_name = "WM_?";
  if ((msg != WM_SETCURSOR && msg != WM_NCHITTEST && msg != WM_MOUSEFIRST
       && msg != WM_ERASEBKGND && msg != WM_CTLCOLORDLG && msg != WM_PRINTCLIENT && msg != WM_CTLCOLORBTN
       && msg != WM_ENTERIDLE
       && (msg != WM_NOTIFY || (LOWORD(wParam) == IDCX_TREEVIEW && ((LPNMHDR) lParam)->code == TVN_SELCHANGED))
     )) {
    for (uint i = 0; i < lengthof(wm_names); i++)
      if (msg == wm_names[i].wm_) {
        wm_name = wm_names[i].wm_name;
        break;
      }
    printf("[%d] dialog_proc %04X %s (%04X %08X)\n", (int)time(0), msg, wm_name, (unsigned)wParam, (unsigned)lParam);
  }
#endif
  switch (msg) {
    when WM_INITDIALOG: {
      ctrlbox = ctrl_new_box();
      setup_config_box(ctrlbox);
      windlg_init();
      winctrl_init(&ctrls_base);
      winctrl_init(&ctrls_panel);
      windlg_add_tree(&ctrls_base);
      windlg_add_tree(&ctrls_panel);
      copy_config("dialog", &new_cfg, &file_cfg);

      RECT r;
      GetWindowRect(GetParent(wnd), &r);
      dlg.wnd = wnd;

     /*
      * Create the actual GUI widgets.
      */
      // here we need the correct DIALOG_HEIGHT already
      create_controls(wnd, "");        /* Open and Cancel buttons etc */

      SendMessage(wnd, WM_SETICON, (WPARAM) ICON_BIG,
                  (LPARAM) LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON)));

     /*
      * Create the tree view.
      */
      r.left = 3;
      r.right = r.left + 64;
      r.top = 3;
      r.bottom = r.top + DIALOG_HEIGHT - 26;
      MapDialogRect(wnd, &r);
      HWND treeview =
        CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
                       TVS_DISABLEDRAGDROP | TVS_HASBUTTONS | TVS_LINESATROOT
                       | TVS_SHOWSELALWAYS, r.left, r.top, r.right - r.left,
                       r.bottom - r.top, wnd, (HMENU) IDCX_TREEVIEW, inst,
                       null);
      WPARAM font = SendMessage(wnd, WM_GETFONT, 0, 0);
      SendMessage(treeview, WM_SETFONT, font, MAKELPARAM(true, 0));
      treeview_faff tvfaff;
      tvfaff.treeview = treeview;
      memset(tvfaff.lastat, 0, sizeof(tvfaff.lastat));

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
        * We expect never to find an implicit path component. 
          For example, we expect never to see A/B/C followed by A/D/E, 
          because that would _implicitly_ create A/D. 
          All our path prefixes are expected to contain actual controls 
          and be selectable in the treeview; so we would expect 
          to see A/D _explicitly_ before encountering A/D/E.
        */

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
        * This should also cause creation of the relevant controls.
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

    when WM_CLOSE:
      DestroyWindow(wnd);

    when WM_DESTROY:
      winctrl_cleanup(&ctrls_base);
      winctrl_cleanup(&ctrls_panel);
      ctrl_free_box(ctrlbox);
      config_wnd = 0;

#ifdef debug_dialog_crash
      signal(SIGSEGV, SIG_DFL);
#endif

    when WM_USER: {
      debug("WM_USER");
      HWND target = (HWND)wParam;
      // could delegate this to winctrls.c, like winctrl_handle_command;
      // but then we'd have to fiddle with the location of dragndrop
     /*
      * Look up the window handle in our data; find the control.
        (Hmm, apparently it works without looking for the widget entry 
        that was particularly introduced for this purpose...)
      */
      control * ctrl = null;
      for (winctrl *c = ctrls_panel.first; c && !ctrl; c = c->next) {
        if (c->ctrl)
          for (int k = 0; k < c->num_ids; k++) {
#ifdef debug_dragndrop
            printf(" [->%8p] %8p\n", target, GetDlgItem(wnd, c->base_id + k));
#endif
            if (target == GetDlgItem(wnd, c->base_id + k)) {
              ctrl = c->ctrl;
              break;
            }
        }
      }
      debug("WM_USER: lookup");
      if (ctrl) {
        //dlg_editbox_set_w(ctrl, L"Test");  // may hit unrelated items...
        // drop the drag-and-drop contents here
        dragndrop = (wstring)lParam;
        ctrl->handler(ctrl, EVENT_DROP);
        debug("WM_USER: handler");
      }
      debug("WM_USER: end");
    }

    when WM_NOTIFY: {
      if (LOWORD(wParam) == IDCX_TREEVIEW &&
          ((LPNMHDR) lParam)->code == TVN_SELCHANGED) {
        debug("WM_NOTIFY");
        HTREEITEM i = TreeView_GetSelection(((LPNMHDR) lParam)->hwndFrom);
        debug("WM_NOTIFY: GetSelection");

        TVITEM item;
        TCHAR buffer[64];
        item.hItem = i;
        item.pszText = buffer;
        item.cchTextMax = lengthof(buffer);
        item.mask = TVIF_TEXT | TVIF_PARAM;
        TreeView_GetItem(((LPNMHDR) lParam)->hwndFrom, &item);

       /* Destroy all controls in the currently visible panel. */
        for (winctrl *c = ctrls_panel.first; c; c = c->next) {
          for (int k = 0; k < c->num_ids; k++) {
            HWND item = GetDlgItem(wnd, c->base_id + k);
            if (item)
              DestroyWindow(item);
          }
        }
        debug("WM_NOTIFY: Destroy");
        winctrl_cleanup(&ctrls_panel);
        debug("WM_NOTIFY: cleanup");

        // here we need the correct DIALOG_HEIGHT already
        create_controls(wnd, (char *) item.lParam);
        debug("WM_NOTIFY: create");
        dlg_refresh(null); /* set up control values */
        debug("WM_NOTIFY: refresh");
      }
    }

    when WM_COMMAND or WM_DRAWITEM: {
      debug("WM_COMMAND");
      int ret = winctrl_handle_command(msg, wParam, lParam);
      debug("WM_COMMAND: handle");
      if (dlg.ended) {
        DestroyWindow(wnd);
        debug("WM_COMMAND: Destroy");
      }
      debug("WM_COMMAND: end");
      return ret;
    }
  }
#ifdef debug_messages
//  catch return above
//  printf(" end dialog_proc %04X %s (%04X %08X)\n", msg, wm_name, (unsigned)wParam, (unsigned)lParam);
#endif
  return 0;
}

HWND config_wnd;

void
win_open_config(void)
{
  if (config_wnd)
    return;

#ifdef debug_dialog_crash
  signal(SIGSEGV, sigsegv);
#endif

  set_dpi_auto_scaling(true);

  static bool initialised = false;
  if (!initialised) {
    InitCommonControls();
    RegisterClassW(&(WNDCLASSW){
      .lpszClassName = W(DIALOG_CLASS),
      .lpfnWndProc = DefDlgProcW,
      .style = CS_DBLCLKS,
      .cbClsExtra = 0,
      .cbWndExtra = DLGWINDOWEXTRA + 2 * sizeof(LONG_PTR),
      .hInstance = inst,
      .hIcon = null,
      .hCursor = LoadCursor(null, IDC_ARROW),
      .hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1),
      .lpszMenuName = null
    });
    initialised = true;
  }

  config_wnd = CreateDialog(inst, MAKEINTRESOURCE(IDD_MAINBOX),
                            wnd, config_dialog_proc);
  // At this point, we could actually calculate the size of the 
  // dialog box used for the Options menu; however, the resulting 
  // value(s) (here DIALOG_HEIGHT) is already needed before this point, 
  // as the callback config_dialog_proc sets up dialog box controls.
  // How insane is that resource concept! Shouldn't I know my own geometry?
  determine_geometry(config_wnd);  // dummy call

  // Set title of Options dialog explicitly to facilitate I18N
  //__ Options: dialog title
  SendMessageW(config_wnd, WM_SETTEXT, 0, (LPARAM)_W("Options"));

  ShowWindow(config_wnd, SW_SHOW);

  set_dpi_auto_scaling(false);
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

static wstring oklabel = null;
static int oktype = MB_OK;

static LRESULT CALLBACK
set_labels(int nCode, WPARAM wParam, LPARAM lParam)
{
  (void)lParam;

#define dont_debug_message_box

  void setlabel(int id, wstring label) {
    HWND button = GetDlgItem((HWND)wParam, id);
#ifdef debug_message_box
    if (button) {
      wchar buf [99];
      GetWindowTextW(button, buf, 99);
      printf("%d [%8p] <%ls> -> <%ls>\n", id, button, buf, label);
    }
    else
      printf("%d %% (<%ls>)\n", id, label);
#endif
    if (button)
      SetWindowTextW(button, label);
  }

  if (nCode == HCBT_ACTIVATE) {
#ifdef debug_message_box
    bool from_mouse = ((CBTACTIVATESTRUCT *)lParam)->fMouse;
    printf("HCBT_ACTIVATE (mou %d) OK %d ok <%ls>\n", from_mouse, (oktype & MB_TYPEMASK) == MB_OK, oklabel);
#endif
    if (!hooked_window_activated) {
      // we need to distinguish re-focussing from initial activation
      // in order to avoid repeated setting of dialog item labels
      // because the IDOK item is renumbered after initial setting
      // (so we would overwrite the wrong label) - what a crap!
      // alternative check:
      // if (!((CBTACTIVATESTRUCT *)lParam)->fMouse)
      //   but that fails if re-focussing is done without mouse (e.g. Alt+TAB)
      if ((oktype & MB_TYPEMASK) == MB_OK)
        setlabel(IDOK, oklabel ?: _W("I see"));
      else
        setlabel(IDOK, oklabel ?: _W("OK"));
      setlabel(IDCANCEL, _W("Cancel"));
#ifdef we_would_use_these_in_any_message_box
#warning W -> _W to add the labels to the localization repository
#warning predefine button labels in config.c
      setlabel(IDABORT, W("&Abort"));
      setlabel(IDRETRY, W("&Retry"));
      setlabel(IDIGNORE, W("&Ignore"));
      setlabel(IDYES, oklabel ?: W("&Yes"));
      setlabel(IDNO, W("&No"));
      //IDCLOSE has no label
      setlabel(IDHELP, W("Help"));
      setlabel(IDTRYAGAIN, W("&Try Again"));
      setlabel(IDCONTINUE, W("&Continue"));
#endif
    }

    hooked_window_activated = true;
  }

  return CallNextHookEx(0, nCode, wParam, lParam);
}


int
message_box(HWND parwnd, char * text, char * caption, int type, wstring ok)
{
  if (!text)
    return 0;
  if (!caption)
    caption = _("Error");

  oklabel = ok;
  oktype = type;
  hook_windows(set_labels);
  int ret;
  if (nonascii(text) || nonascii(caption)) {
    wchar * wtext = cs__utftowcs(text);
    wchar * wcapt = cs__utftowcs(caption);
    ret = MessageBoxW(parwnd, wtext, wcapt, type);
    free(wtext);
    free(wcapt);
  }
  else
    ret = MessageBoxA(parwnd, text, caption, type);
  unhook_windows();
  return ret;
}

int
message_box_w(HWND parwnd, wchar * wtext, wchar * wcaption, int type, wstring ok)
{
  if (!wtext)
    return 0;
  if (!wcaption)
    wcaption = _W("Error");

  oklabel = ok;
  oktype = type;
  hook_windows(set_labels);
  int ret;
  ret = MessageBoxW(parwnd, wtext, wcaption, type);
  unhook_windows();
  return ret;
}

void
win_show_about(void)
{
#if CYGWIN_VERSION_API_MINOR < 74
  char * aboutfmt = newn(char, 
    strlen(VERSION_TEXT) + strlen(COPYRIGHT) + strlen(LICENSE_TEXT) + strlen(_(WARRANTY_TEXT)) + strlen(_(ABOUT_TEXT)) + 11);
  sprintf(aboutfmt, "%s\n%s\n%s\n%s\n\n%s", 
           VERSION_TEXT, COPYRIGHT, LICENSE_TEXT, _(WARRANTY_TEXT), _(ABOUT_TEXT));
  char * abouttext = newn(char, strlen(aboutfmt) + strlen(WEBSITE));
  sprintf(abouttext, aboutfmt, WEBSITE);
#else
  char * aboutfmt =
    asform("%s\n%s\n%s\n%s\n\n%s", 
           VERSION_TEXT, COPYRIGHT, LICENSE_TEXT, _(WARRANTY_TEXT), _(ABOUT_TEXT));
  char * abouttext = asform(aboutfmt, WEBSITE);
#endif
  free(aboutfmt);
  wchar * wmsg = cs__utftowcs(abouttext);
  free(abouttext);
  oklabel = null;
  oktype = MB_OK;
  hook_windows(set_labels);
  MessageBoxIndirectW(&(MSGBOXPARAMSW){
    .cbSize = sizeof(MSGBOXPARAMSW),
    .hwndOwner = config_wnd,
    .hInstance = inst,
    .lpszCaption = W(APPNAME),
    .dwStyle = MB_USERICON | MB_OK,
    .lpszIcon = MAKEINTRESOURCEW(IDI_MAINICON),
    .lpszText = wmsg
  });
  unhook_windows();
  free(wmsg);
}

void
win_show_error(char * msg)
{
  message_box(0, msg, null, MB_ICONERROR, 0);
}

void
win_show_warning(char * msg)
{
  message_box(0, msg, null, MB_ICONWARNING, 0);
}

