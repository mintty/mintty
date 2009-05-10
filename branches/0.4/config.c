// config.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"

#include "ctrls.h"
#include "print.h"
#include "unicode.h"
#include "term.h"
#include "win.h"

const char *log_file = 0;
bool utmp_enabled = false;
hold_t hold = HOLD_NEVER;

const char *config_file = 0;
config cfg, new_cfg;

static control *cols_box, *rows_box;

static void
apply_config(void)
{
  win_reconfig();
  save_config();
}

static void
ok_handler(control *unused(ctrl), void *dlg,
           void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    apply_config();
    dlg_end(dlg);
  }
}

static void
cancel_handler(control *unused(ctrl), void *dlg,
               void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    dlg_end(dlg);
}

static void
apply_handler(control *unused(ctrl), void *unused(dlg),
           void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    apply_config();
}

static void
about_handler(control *unused(ctrl), void *unused(dlg),
           void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    win_show_about();
}

static void
current_size_handler(control *unused(ctrl), void *dlg,
           void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    new_cfg.cols = term_cols();
    new_cfg.rows = term_rows();
    dlg_refresh(cols_box, dlg);
    dlg_refresh(rows_box, dlg);
  }
}

const char PRINTER_DISABLED_STRING[] = "None (printing disabled)";

static void
printerbox_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  if (event == EVENT_REFRESH) {
    int nprinters, i;
    printer_enum *pe;

    dlg_update_start(ctrl, dlg);
   /*
    * Some backends may wish to disable the drop-down list on
    * this edit box. Be prepared for this.
    */
    if (ctrl->editbox.has_list) {
      dlg_listbox_clear(ctrl, dlg);
      dlg_listbox_add(ctrl, dlg, PRINTER_DISABLED_STRING);
      pe = printer_start_enum(&nprinters);
      for (i = 0; i < nprinters; i++)
        dlg_listbox_add(ctrl, dlg, printer_get_name(pe, i));
      printer_finish_enum(pe);
    }
    dlg_editbox_set(
      ctrl, dlg, 
      *new_cfg.printer ? new_cfg.printer : PRINTER_DISABLED_STRING
    );
    dlg_update_done(ctrl, dlg);
  }
  else if (event == EVENT_VALCHANGE) {
    dlg_editbox_get(ctrl, dlg, new_cfg.printer, sizeof (cfg.printer));
    if (strcmp(new_cfg.printer, PRINTER_DISABLED_STRING) == 0)
      *new_cfg.printer = '\0';
  }
}

static void
codepage_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  char *cp = new_cfg.codepage;
  if (event == EVENT_REFRESH) {
    dlg_update_start(ctrl, dlg);
    strcpy(cp, cp_name(decode_codepage(cp)));
    dlg_listbox_clear(ctrl, dlg);
    const char *icp;
    for (int i = 0; (icp = cp_enumerate(i)); i++)
      dlg_listbox_add(ctrl, dlg, icp);
    dlg_editbox_set(ctrl, dlg, cp);
    dlg_update_done(ctrl, dlg);
  }
  else if (event == EVENT_VALCHANGE) {
    dlg_editbox_get(ctrl, dlg, cp, sizeof (cfg.codepage));
    strcpy(cp, cp_name(decode_codepage(cp)));
  }
}

static void
colour_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  colour *colour_p = ctrl->context.p;
  if (event == EVENT_ACTION) {
   /*
    * Start a colour selector, which will send us an
    * EVENT_CALLBACK when it's finished and allow us to
    * pick up the results.
    */
    dlg_coloursel_start(dlg, *colour_p);
  }
  else if (event == EVENT_CALLBACK) {
   /*
    * Collect the results of the colour selector. Will
    * return nonzero on success, or zero if the colour
    * selector did nothing (user hit Cancel, for example).
    */
    colour result;
    if (dlg_coloursel_results(dlg, &result))
      *colour_p = result;
  }
}


#define offcfg(setting) offsetof(config, setting)

void
setup_config_box(controlbox * b)
{
  controlset *s;
  control *c;

 /*
  * The standard panel that appears at the bottom of all panels:
  * Open, Cancel, Apply etc.
  */
  s = ctrl_getset(b, "", "", "");
  ctrl_columns(s, 5, 20, 20, 20, 20, 20);
  c = ctrl_pushbutton(s, "About...", 0, P(0), about_handler, P(0));
  c->column = 0;
  c = ctrl_pushbutton(s, "OK", 0, P(0), ok_handler, P(0));
  c->button.isdefault = true;
  c->column = 2;
  c = ctrl_pushbutton(s, "Cancel", 0, P(0), cancel_handler, P(0));
  c->button.iscancel = true;
  c->column = 3;
  c = ctrl_pushbutton(s, "Apply", 0, P(0), apply_handler, P(0));
  c->column = 4;

 /*
  * The Window panel.
  */
  ctrl_settitle(b, "Window", "Window");

  s = ctrl_getset(b, "Window", "size", "Default size");
  ctrl_columns(s, 5, 35, 3, 28, 4, 30);
  (cols_box = ctrl_editbox(
    s, "Columns", 'o', 44, P(0),
    dlg_stdeditbox_handler, I(offcfg(cols)), I(-1)
  ))->column = 0;
  (rows_box = ctrl_editbox(
    s, "Rows", 'r', 55, P(0),
    dlg_stdeditbox_handler, I(offcfg(rows)), I(-1)
  ))->column = 2;
  ctrl_pushbutton(
    s, "Current size", 'u', P(0), current_size_handler, P(0)
  )->column = 4;

  s = ctrl_getset(b, "Window", "options", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Show scrollbar", 'b', P(0),
    dlg_stdcheckbox_handler, I(offcfg(scrollbar))
  )->column = 0;
  ctrl_checkbox(
    s, "Confirm exit", 'b', P(0),
    dlg_stdcheckbox_handler, I(offcfg(confirm_exit))
  )->column = 1;

  s = ctrl_getset(b, "Window", "scrollback", "Scrollback");
  ctrl_columns(s, 2, 35, 65);
  ctrl_editbox(
    s, "Lines", 'l', 65, P(0),
    dlg_stdeditbox_handler, I(offsetof(config, scrollback_lines)), I(-1)
  )->column = 0;
  ctrl_columns(s, 1, 100);
  ctrl_radiobuttons(
    s, "Modifier for scrolling with cursor keys", '\0', 4, P(0),      
    dlg_stdradiobutton_handler, I(offcfg(scroll_mod)),
    "Shift", 's', I(SHIFT),
    "Ctrl", 'c', I(CTRL),
    "Alt", 'a', I(ALT),
    null
  );

 /*
  * The Looks panel.
  */
  ctrl_settitle(b, "Looks", "Looks");

  s = ctrl_getset(b, "Looks", "colours", "Colours");
  ctrl_columns(s, 3, 33, 33, 33);
  ctrl_pushbutton(
    s, "Foreground...", 'f', P(0), colour_handler, P(&new_cfg.fg_colour)
  )->column = 0;
  ctrl_pushbutton(
    s, "Background...", 'b', P(0), colour_handler, P(&new_cfg.bg_colour)
  )->column = 1;
  ctrl_pushbutton(
    s, "Cursor...", 'c', P(0), colour_handler, P(&new_cfg.cursor_colour)
  )->column = 2;

  s = ctrl_getset(b, "Looks", "trans", "Transparency");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(transparency)),
    "Off", 'o', I(0),
    "Low", 'l', I(1),
    "Medium", 'm', I(2), 
    "High", 'h', I(3), 
    null
  );
  ctrl_checkbox(
    s, "Opaque when focused", 'p', P(0),
    dlg_stdcheckbox_handler, I(offcfg(opaque_when_focused))
  );

  s = ctrl_getset(b, "Looks", "curtype", "Cursor");
  ctrl_radiobuttons(
    s, null, '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(cursor_type)),
    "Line", 'n', I(CUR_LINE), 
    "Underline", 'u', I(CUR_UNDERLINE),
    "Block", 'k', I(CUR_BLOCK),
    null
  );
  ctrl_checkbox(
     s, "Enable blinking", 'e', P(0),
     dlg_stdcheckbox_handler, I(offcfg(cursor_blinks))
  );

 /*
  * The Text panel.
  */
  ctrl_settitle(b, "Text", "Text");

  s = ctrl_getset(b, "Text", "font", null);
  ctrl_fontsel(
    s, null, 'f', P(0), dlg_stdfontsel_handler, I(offcfg(font))
  );
  ctrl_radiobuttons(
    s, "Smoothing", '\0', 2, P(0), dlg_stdradiobutton_handler, 
    I(offcfg(font_quality)),
    "System Default", 'd', I(FQ_DEFAULT),
    "Antialiased", 'a', I(FQ_ANTIALIASED),
    "None", 'n', I(FQ_NONANTIALIASED),
    "ClearType", 'c', I(FQ_CLEARTYPE),
    null
  );

  s = ctrl_getset(b, "Text", "effects", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Show bold as bright", 's', P(0), dlg_stdcheckbox_handler,
    I(offcfg(bold_as_bright))
  )->column = 0;
  ctrl_checkbox(
    s, "Allow blinking", 'b', P(0),
    dlg_stdcheckbox_handler, I(offcfg(allow_blinking))
  )->column = 1;

  s = ctrl_getset(b, "Text", "codepage", "Codepage");
  ctrl_combobox(s, null, '\0', 100, P(0), codepage_handler, P(null), P(null));

 /*
  * The Keys panel.
  */
  ctrl_settitle(b, "Keys", "Keys");

  s = ctrl_getset(b, "Keys", "keycodes", "Keycodes");
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Escape", '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(escape_sends_fs)),
    "^[", '[', I(0),
    "^\\", '\\', I(1),
    null
  )->column = 0;
  ctrl_radiobuttons(
    s, "Backspace", '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(backspace_sends_del)),
    "^H", 'h', I(0),
    "^?", '?', I(1),
    null
  )->column = 1;

  s = ctrl_getset(b, "Keys", "alt", null);
  ctrl_checkbox(
    s, "Alt key on its own sends ^[", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(alt_sends_esc))
  );

  s = ctrl_getset(b, "Keys", "shortcuts", "Shortcuts");
  ctrl_checkbox(
    s, "Window commands (Alt+Space/Enter/Fn)", 'd', P(0),
    dlg_stdcheckbox_handler, I(offcfg(window_shortcuts))
  );
  ctrl_checkbox(
    s, "Copy and paste (Ctrl/Shift+Ins)", 'c', P(0),
    dlg_stdcheckbox_handler, I(offcfg(edit_shortcuts))
  );
  ctrl_checkbox(
    s, "Zoom (Ctrl+plus/minus/zero)", 'f', P(0),
    dlg_stdcheckbox_handler, I(offcfg(zoom_shortcuts))
  );
  
 /*
  * The Mouse panel.
  */
  ctrl_settitle(b, "Mouse", "Mouse");

  s = ctrl_getset(b, "Mouse", "rightclick", "Right click action");
  ctrl_radiobuttons(
    s, null, '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(right_click_action)),
    "Show menu", 'm', I(RC_SHOWMENU),
    "Extend", 'x', I(RC_EXTEND),
    "Paste", 'p', I(RC_PASTE),
    null
  );
  
  s = ctrl_getset(b, "Mouse", "mouseopts", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Copy on select", 'y', P(0),
    dlg_stdcheckbox_handler, I(offcfg(copy_on_select))
  )->column = 0;
  ctrl_checkbox(
    s, "Clicks place cursor", 'v', P(0),
    dlg_stdcheckbox_handler, I(offcfg(clicks_place_cursor))
  )->column = 1;

  s = ctrl_getset(b, "Mouse", "mousemode", "Application mouse mode");
  ctrl_radiobuttons(
    s, "Default click target", '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(clicks_target_app)),
    "Application", 'n', I(1),
    "Window", 'w', I(0),
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for overriding default", '\0', 4, P(0),
    dlg_stdradiobutton_handler, I(offcfg(click_target_mod)),
    "Shift", 's', I(SHIFT),
    "Ctrl", 'c', I(CTRL),
    "Alt", 'a', I(ALT),
    null
  );
  
 /*
  * The Output panel.
  */
  ctrl_settitle(b, "Output", "Output");

  s = ctrl_getset(b, "Output", "printer", "Printer");
  ctrl_combobox(
    s, null, '\0', 100, P(0), printerbox_handler, P(null), P(null)
  );

  s = ctrl_getset(b, "Output", "bell", "Bell");
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Action", '\0', 1, P(0), dlg_stdradiobutton_handler, 
    I(offcfg(bell_type)),
    "None", 'n', I(BELL_DISABLED),
    "System sound", 's', I(BELL_SOUND),
    "Flash window", 'f', I(BELL_VISUAL),
    null
  )->column = 0;
  ctrl_radiobuttons(
    s, "Taskbar indication", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offcfg(bell_ind)),
    "Disabled", 'd', I(B_IND_DISABLED),
    "Steady", 'y', I(B_IND_STEADY),
    "Blinking", 'b', I(B_IND_FLASH),
    null
  )->column = 1;
}


typedef const struct {
  const char *key;
  ushort offset;
  ushort def;
} int_setting;

static int_setting
int_settings[] = {
  {"Columns", offcfg(cols), 80},
  {"Rows", offcfg(rows), 24},
  {"Transparency", offcfg(transparency), 0},
  {"OpaqueWhenFocused", offcfg(opaque_when_focused), 0},
  {"Scrollbar", offcfg(scrollbar), true},
  {"ScrollbackLines", offcfg(scrollback_lines), 10000},
  {"ConfirmExit", offcfg(confirm_exit), true},
  {"WindowShortcuts", offcfg(window_shortcuts), true},
  {"EditShortcuts", offcfg(edit_shortcuts), true},
  {"ZoomShortcuts", offcfg(zoom_shortcuts), true},
  {"BoldAsBright", offcfg(bold_as_bright), true},
  {"AllowBlinking", offcfg(allow_blinking), false},
  {"CursorType", offcfg(cursor_type), 2},
  {"CursorBlinks", offcfg(cursor_blinks), true},
  {"FontIsBold", offcfg(font.isbold), 0},
  {"FontHeight", offcfg(font.size), 10},
  {"FontCharset", offcfg(font.charset), 0},
  {"FontQuality", offcfg(font_quality), FQ_DEFAULT},
  {"BackspaceSendsDEL", offcfg(backspace_sends_del), false},
  {"EscapeSendsFS", offcfg(escape_sends_fs), false},
  {"AltSendsESC", offcfg(alt_sends_esc), false},
  {"ScrollMod", offcfg(scroll_mod), SHIFT},
  {"RightClickAction", offcfg(right_click_action), RC_SHOWMENU},
  {"CopyOnSelect", offcfg(copy_on_select), false},
  {"ClicksPlaceCursor", offcfg(clicks_place_cursor), true},
  {"ClicksTargetApp", offcfg(clicks_target_app), true},
  {"ClickTargetMod", offcfg(click_target_mod), SHIFT},
  {"BellType", offcfg(bell_type), BELL_SOUND},
  {"BellIndication", offcfg(bell_ind), B_IND_STEADY},
};

typedef const struct {
  const char *key;
  ushort offset;
  ushort len;
  const char *def;
} string_setting;

static string_setting
string_settings[] = {
  {"Font", offcfg(font.name), sizeof cfg.font.name, "Lucida Console"},
  {"Codepage", offcfg(codepage), sizeof cfg.codepage, ""},
  {"Printer", offcfg(printer), sizeof cfg.printer, ""},
};

typedef const struct {
  const char *key;
  ushort offset;
  colour def;
} colour_setting;

static colour_setting
colour_settings[] = {
  {"ForegroundColour", offcfg(fg_colour), 0xBFBFBF},
  {"BackgroundColour", offcfg(bg_colour), 0x000000},
  {"CursorColour", offcfg(cursor_colour), 0xBFBFBF},
};

void
load_config(void)
{
  open_settings_r(config_file);
  for (int_setting *s = int_settings; s < endof(int_settings); s++)
    read_int_setting(s->key, &atoffset(int, &cfg, s->offset), s->def);
  for (string_setting *s = string_settings; s < endof(string_settings); s++)
    read_string_setting(s->key, &atoffset(char, &cfg, s->offset), s->len, s->def);
  for (colour_setting *s = colour_settings; s < endof(colour_settings); s++)
    read_colour_setting(s->key, &atoffset(colour, &cfg, s->offset), s->def);
  close_settings_r();
}

char *
save_config(void)
{
  char *errmsg = open_settings_w(config_file);
  if (errmsg)
    return errmsg;
  for (int_setting *s = int_settings; s < endof(int_settings); s++)
    write_int_setting(s->key, atoffset(int, &cfg, s->offset));
  for (string_setting *s = string_settings; s < endof(string_settings); s++)
    write_string_setting(s->key, &atoffset(char, &cfg, s->offset));
  for (colour_setting *s = colour_settings; s < endof(colour_settings); s++)
    write_colour_setting(s->key, atoffset(colour, &cfg, s->offset));
  close_settings_w();
  return 0;
}

