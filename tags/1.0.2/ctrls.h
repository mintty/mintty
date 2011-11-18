#ifndef CTRLS_H
#define CTRLS_H

#include "config.h"

/*
 * This is the big union which defines a single control, of any
 * type.
 * 
 * General principles:
 *  - _All_ pointers in this structure are expected to point to
 *    dynamically allocated things, unless otherwise indicated.
 *  - The `label' field can often be null, which will cause the
 *    control to not have a label at all. This doesn't apply to
 *    checkboxes and push buttons, in which the label is not
 *    separate from the control.
 */

enum {
  CTRL_EDITBOX,    /* label plus edit box */
  CTRL_RADIO,      /* label plus radio buttons */
  CTRL_CHECKBOX,   /* checkbox (contains own label) */
  CTRL_BUTTON,     /* simple push button (no label) */
  CTRL_LISTBOX,    /* label plus list box */
  CTRL_COLUMNS,    /* divide window into columns */
  CTRL_FONTSELECT, /* label plus font selector */
};

/*
 * Each control has an `int' field specifying which columns it
 * occupies in a multi-column part of the dialog box. These macros
 * pack and unpack that field.
 * 
 * If a control belongs in exactly one column, just specifying the
 * column number is perfectly adequate.
 */
#define COLUMN_FIELD(start, span) ( (((span)-1) << 16) + (start) )
#define COLUMN_START(field) ( (field) & 0xFFFF )
#define COLUMN_SPAN(field) ( (((field) >> 16) & 0xFFFF) + 1 )

/*
 * The number of event types is being deliberately kept small, on
 * the grounds that not all platforms might be able to report a
 * large number of subtle events. We have:
 *  - the special REFRESH event, called when a control's value
 *    needs setting
 *  - the ACTION event, called when the user does something that
 *    positively requests action (double-clicking a list box item,
 *    or pushing a push-button)
 *  - the VALCHANGE event, called when the user alters the setting
 *    of the control in a way that is usually considered to alter
 *    the underlying data (toggling a checkbox or radio button,
 *    moving the items around in a drag-list, editing an edit
 *    control)
 *  - the SELCHANGE event, called when the user alters the setting
 *    of the control in a more minor way (changing the selected
 *    item in a list box).
 *  - the CALLBACK event, which happens after the handler routine
 *    has requested a subdialog (file selector, font selector,
 *    colour selector) and it has come back with information.
 */
enum {
  EVENT_REFRESH,
  EVENT_ACTION,
  EVENT_VALCHANGE,
  EVENT_SELCHANGE,
  EVENT_UNFOCUS,
  EVENT_CALLBACK
};

typedef struct control control;

typedef void (*handler_fn)(control *, int event);

struct control {
  int type;
 /*
  * Every control except CTRL_COLUMNS has _some_ sort of
  * label. By putting it in the `generic' union as well as
  * everywhere else, we avoid having to have an irritating
  * switch statement when we go through and deallocate all
  * the memory in a config-box structure.
  * 
  * Yes, this does mean that any non-null value in this
  * field is expected to be dynamically allocated and
  * freeable.
  * 
  * For CTRL_COLUMNS, this field MUST be null.
  */
  char *label;
 /*
  * Indicate which column(s) this control occupies. This can
  * be unpacked into starting column and column span by the
  * COLUMN macros above.
  */
  int column;
 /*
  * Most controls need to provide a function which gets
  * called when that control's setting is changed, or when
  * the control's setting needs initialising.
  * 
  * The `data' parameter points to the writable data being
  * modified as a result of the configuration activity; for
  * example, the PuTTY `Config' structure, although not
  * necessarily.
  * 
  * The `dlg' parameter is passed back to the platform-
  * specific routines to read and write the actual control
  * state.
  */
  handler_fn handler;
 /*
  * Almost all of the above functions will find it useful to
  * be able to store a piece of `void *' data.
  */
  void *context;
  union {
    struct {
     /*
      * Percentage of the dialog-box width used by the edit box.
      * If this is set to 100, the label is on its own line;
      * otherwise the label is on the same line as the box
      * itself.
      */
      int percentwidth;
      int password;       /* details of input are hidden */
     /*
      * A special case of the edit box is the combo box, which
      * has a drop-down list built in. (Note that a _non_-
      * editable drop-down list is done as a special case of a
      * list box.)
      */
      int has_list;
    } editbox;
    struct {
     /*
      * There are separate fields for `ncolumns' and `nbuttons'
      * for several reasons.
      * 
      * Firstly, we sometimes want the last of a set of buttons
      * to have a longer label than the rest; we achieve this by
      * setting `ncolumns' higher than `nbuttons', and the
      * layout code is expected to understand that the final
      * button should be given all the remaining space on the
      * line. This sounds like a ludicrously specific special
      * case (if we're doing this sort of thing, why not have
      * the general ability to have a particular button span
      * more than one column whether it's the last one or not?)
      * but actually it's reasonably common for the sort of
      * three-way control you get a lot of in PuTTY: `yes'
      * versus `no' versus `some more complex way to decide'.
      * 
      * Secondly, setting `nbuttons' higher than `ncolumns' lets
      * us have more than one line of radio buttons for a single
      * setting. A very important special case of this is
      * setting `ncolumns' to 1, so that each button is on its
      * own line.
      */
      int ncolumns;
      int nbuttons;
     /*
      * This points to a dynamically allocated array of `char *'
      * pointers, each of which points to a dynamically
      * allocated string.
      */
      string *labels;     /* `nbuttons' button labels */
     /*
      * This points to a dynamically allocated array,
      * with the value corresponding to each button.
      */
      int *vals;         /* `nbuttons' entries; may be null */
    } radio;
    struct {
     /*
      * At least Windows has the concept of a `default push
      * button', which gets implicitly pressed when you hit
      * Return even if it doesn't have the input focus.
      */
      int isdefault;
     /*
      * Also, the reverse of this: a default cancel-type button,
      * which is implicitly pressed when you hit Escape.
      */
      int iscancel;
    } button;
    struct {
     /*
      * Height of the list box, in approximate number of lines.
      * If this is zero, the list is a drop-down list.
      */
      int height; /* height in lines */
     /*
      * Percentage of the dialog-box width used by the list box.
      * If this is set to 100, the label is on its own line;
      * otherwise the label is on the same line as the box
      * itself. Setting this to anything other than 100 is not
      * guaranteed to work on a _non_-drop-down list, so don't
      * try it!
      */
      int percentwidth;
     /*
      * Some list boxes contain strings that contain tab
      * characters. If `ncols' is greater than 0, then
      * `percentages' is expected to be non-zero and to contain
      * the respective widths of `ncols' columns, which together
      * will exactly fit the width of the list box. Otherwise
      * `percentages' must be null.
      */
      int ncols;  /* number of columns */
      int *percentages;   /* % width of each column */
    } listbox;

    struct {
     /* In this variant, `label' MUST be null. */
      int ncols;  /* number of columns */
      int *percentages;   /* % width of each column */
     /*
      * Every time this control type appears, exactly one of
      * `ncols' and the previous number of columns MUST be one.
      * Attempting to allow a seamless transition from a four-
      * to a five-column layout, for example, would be way more
      * trouble than it was worth. If you must lay things out
      * like that, define eight unevenly sized columns and use
      * column-spanning a lot. But better still, just don't.
      * 
      * `percentages' may be null if ncols==1, to save space.
      */
    } columns;
  };
  
  /* Space for storing platform-specific control data */
  void *plat_ctrl;
};

#undef STANDARD_PREFIX

/*
 * `controlset' is a container holding an array of `control'
 * structures, together with a panel name and a title for the whole
 * set. In Windows and any similar-looking GUI, each `controlset'
 * in the config will be a container box within a panel.
 */
typedef struct {
  char *pathname;       /* panel path, e.g. "SSH/Tunnels" */
  char *boxtitle;       /* title of container box */
  int ncolumns; /* current no. of columns at bottom */
  int ncontrols;        /* number of `control' in array */
  int ctrlsize; /* allocated size of array */
  control **ctrls;      /* actual array */
} controlset;

/*
 * This is the container structure which holds a complete set of
 * controls.
 */
typedef struct {
  int nctrlsets;        /* number of ctrlsets */
  int ctrlsetsize;      /* ctrlset size */
  controlset **ctrlsets;        /* actual array of ctrlsets */
  int nfrees;
  int freesize;
  void **frees; /* array of aux data areas to free */
} controlbox;

controlbox *ctrl_new_box(void);
void ctrl_free_box(controlbox *);

/*
 * Standard functions used for populating a controlbox structure.
 */

/* Create a controlset. */
controlset *ctrl_new_set(controlbox *, char *path, char *title);
void ctrl_free_set(controlset *);

void ctrl_free(control *);

/*
 * This function works like `malloc', but the memory it returns
 * will be automatically freed when the controlbox is freed. Note
 * that a controlbox is a dialog-box _template_, not an instance,
 * and so data allocated through this function is better not used
 * to hold modifiable per-instance things. It's mostly here for
 * allocating structures to be passed as control handler params.
 */
void *ctrl_alloc(controlbox *, size_t size);

/*
 * Individual routines to create `control' structures in a controlset.
 * 
 * Most of these routines allow the most common fields to be set
 * directly, and put default values in the rest. Each one returns a
 * pointer to the `control' it created, so that final tweaks
 * can be made.
 */

/* `ncolumns' is followed by that many percentages, as integers. */
control *ctrl_columns(controlset *, int ncolumns, ...);
control *ctrl_editbox(controlset *, char *label, int percentage,
                      handler_fn handler, void *context);
control *ctrl_combobox(controlset *, char *label, int percentage,
                       handler_fn handler, void *context);
/*
 * `ncolumns' is followed by (alternately) radio button titles and
 * integers, until a null in place of a title string is seen.
 */
control *ctrl_radiobuttons(controlset *, char *label, int ncolumns,
                           handler_fn handler, void *context, ...);
control *ctrl_pushbutton(controlset *, char *label,
                         handler_fn handler, void *context);
control *ctrl_droplist(controlset *, char *label, int percentage,
                       handler_fn handler, void *context);
control *ctrl_fontsel(controlset *, char *label,
                      handler_fn handler, void *context);
control *ctrl_checkbox(controlset *, char *label,
                       handler_fn handler, void *context);

/*
 * Standard handler routines to cover most of the common cases in
 * the config box.
 */

void dlg_stdcheckbox_handler(control *, int event);
void dlg_stdstringbox_handler(control *, int event);
void dlg_stdintbox_handler(control *, int event);
void dlg_stdradiobutton_handler(control *, int event);
void dlg_stdfontsel_handler(control *, int event);
void dlg_stdcolour_handler(control *, int event);

/*
 * Routines the platform-independent dialog code can call to read
 * and write the values of controls.
 */
void dlg_radiobutton_set(control *, int whichbutton);
int dlg_radiobutton_get(control *);
void dlg_checkbox_set(control *, bool);
bool dlg_checkbox_get(control *);
void dlg_editbox_set(control *, string);
void dlg_editbox_get(control *, string *);
/* The `listbox' functions also apply to combo boxes. */
void dlg_listbox_clear(control *);
void dlg_listbox_add(control *, string);
void dlg_fontsel_set(control *, font_spec *);
void dlg_fontsel_get(control *, font_spec *);
/*
 * Set input focus into a particular control.
 */
void dlg_set_focus(control *);
/*
 * This function signals to the front end that the dialog's
 * processing is completed.
 */
void dlg_end(void);

/*
 * Routines to manage a (per-platform) colour selector.
 * dlg_coloursel_start() is called in an event handler, and
 * schedules the running of a colour selector after the event
 * handler returns. The colour selector will send EVENT_CALLBACK to
 * the control that spawned it, when it's finished;
 * dlg_coloursel_results() fetches the results, as integers from 0
 * to 255; it returns nonzero on success, or zero if the colour
 * selector was dismissed by hitting Cancel or similar.
 * 
 * dlg_coloursel_start() accepts an RGB triple which is used to
 * initialise the colour selector to its starting value.
 */
void dlg_coloursel_start(colour);
int dlg_coloursel_results(colour *);

/*
 * This routine is used by the platform-independent code to
 * indicate that the value of a particular control is likely to
 * have changed. It triggers a call of the handler for that control
 * with `event' set to EVENT_REFRESH.
 * 
 * If `ctrl' is null, _all_ controls in the dialog get refreshed
 * (for loading or saving entire sets of settings).
 */
void dlg_refresh(control *);

/*
 * Standard helper functions for reading a controlbox structure.
 */

/*
 * Find the index of next controlset in a controlbox for a given
 * path, or -1 if no such controlset exists. If -1 is passed as
 * input, finds the first. Intended usage is something like
 * 
 *      for (index=-1; (index=ctrl_find_path(ctrlbox, index, path)) >= 0 ;) {
 *          ... process this controlset ...
 *      }
 */
int ctrl_find_path(controlbox *, char *path, int index);

/* Return the number of matching path elements at the starts of p1 and p2,
 * or INT_MAX if the paths are identical. */
int ctrl_path_compare(char *p1, char *p2);

#endif
