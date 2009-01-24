// config.c (part of MinTTY)
// Copyright 2008-09 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"

#include "ctrls.h"
#include "print.h"
#include "unicode.h"
#include "term.h"

/*
 * config.c - the configuration box.
 */

char *config_filename;

config cfg;     /* exported to windlg.c */

static void
ok_handler(control *unused(ctrl), void *dlg,
           void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    save_config();
    dlg_end(dlg, 1);
  }
}

static void
cancel_handler(control *unused(ctrl), void *dlg,
               void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    dlg_end(dlg, 0);
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
    dlg_editbox_set(ctrl, dlg,
                    (*cfg.printer ? cfg.printer : PRINTER_DISABLED_STRING));
    dlg_update_done(ctrl, dlg);
  }
  else if (event == EVENT_VALCHANGE) {
    dlg_editbox_get(ctrl, dlg, cfg.printer, sizeof (cfg.printer));
    if (!strcmp(cfg.printer, PRINTER_DISABLED_STRING))
      *cfg.printer = '\0';
  }
}

static void
codepage_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  if (event == EVENT_REFRESH) {
    int i;
    const char *cp;
    dlg_update_start(ctrl, dlg);
    strcpy(cfg.codepage, cp_name(decode_codepage(cfg.codepage)));
    dlg_listbox_clear(ctrl, dlg);
    for (i = 0; (cp = cp_enumerate(i)) != null; i++)
      dlg_listbox_add(ctrl, dlg, cp);
    dlg_editbox_set(ctrl, dlg, cfg.codepage);
    dlg_update_done(ctrl, dlg);
  }
  else if (event == EVENT_VALCHANGE) {
    dlg_editbox_get(ctrl, dlg, cfg.codepage, sizeof (cfg.codepage));
    strcpy(cfg.codepage, cp_name(decode_codepage(cfg.codepage)));
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
  c = ctrl_pushbutton(s, "OK", '\0', P(0), ok_handler, P(0));
  c->button.isdefault = true;
  c->column = 3;
  c = ctrl_pushbutton(s, "Cancel", '\0', P(0), cancel_handler, P(0));
  c->button.iscancel = true;
  c->column = 4;

 /*
  * The Window panel.
  */
  ctrl_settitle(b, "Window", "Window");

  s = ctrl_getset(b, "Window", "size", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_editbox(
    s, "Columns", 'c', 100, P(0),
    dlg_stdeditbox_handler, I(offcfg(cols)), I(-1)
  )->column = 0;
  ctrl_editbox(
    s, "Rows", 'r', 100, P(0),
    dlg_stdeditbox_handler, I(offcfg(rows)), I(-1)
  )->column = 1;

  s = ctrl_getset(b, "Window", "trans", "Transparency");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(transparency)),
    "Off", 'o', I(0),
    "Low", 'l', I(1),
    "Medium", 'm', I(2), 
    "High", 'h', I(3), 
    null
  );

  s = ctrl_getset(b, "Window", "misc", null);
  ctrl_checkbox(
    s, "Disable transparency when active", 'd', P(0),
    dlg_stdcheckbox_handler, I(offcfg(opaque_when_focused))
  );
  ctrl_checkbox(
    s, "Show scrollbar", 's', P(0),
    dlg_stdcheckbox_handler, I(offcfg(scrollbar))
  );
  ctrl_checkbox(
    s, "Close on Alt+F4", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(close_on_alt_f4))
  );
  

 /*
  * The Looks panel.
  */
  ctrl_settitle(b, "Looks", "Looks");

  s = ctrl_getset(b, "Looks", "colours", "Colours");
  ctrl_columns(s, 3, 33, 33, 33);
  ctrl_pushbutton(
    s, "Foreground", 'f', P(0), colour_handler, P(&cfg.fg_colour)
  )->column = 0;
  ctrl_pushbutton(
    s, "Background", 'b', P(0), colour_handler, P(&cfg.bg_colour)
  )->column = 1;
  ctrl_pushbutton(
    s, "Cursor", 'c', P(0), colour_handler, P(&cfg.cursor_colour)
  )->column = 2;

  s = ctrl_getset(b, "Looks", "text", null);
  ctrl_checkbox(
    s, "Show bold text as bright", 's', P(0), dlg_stdcheckbox_handler,
    I(offcfg(bold_as_bright))
  );
  ctrl_checkbox(
    s, "Allow text blinking", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(allow_blinking))
  );

  s = ctrl_getset(b, "Looks", "curtype", "Cursor");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(cursor_type)),
    "Block", 'k', I(CUR_BLOCK),
    "Line", 'l', I(CUR_LINE), 
    "Underline", 'u', I(CUR_UNDERLINE),
    null
  );
  s = ctrl_getset(b, "Looks", "curblink", null);
  ctrl_checkbox(
     s, "Enable cursor blinking", 'e', P(0),
     dlg_stdcheckbox_handler, I(offcfg(cursor_blinks))
  );

 /*
  * The Font panel.
  */
  ctrl_settitle(b, "Font", "Font");

  s = ctrl_getset(b, "Font", "font", null);
  ctrl_fontsel(
    s, null, '\0', P(0), dlg_stdfontsel_handler, I(offcfg(font))
  );

  s = ctrl_getset(b, "Font", "smooth", "Smoothing");
  ctrl_radiobuttons(
    s, null, '\0', 2, P(0), dlg_stdradiobutton_handler, 
    I(offcfg(font_quality)),
    "System Default", 'd', I(FQ_DEFAULT),
    "Antialiased", 'a', I(FQ_ANTIALIASED),
    "Non-Antialiased", 'n', I(FQ_NONANTIALIASED),
    "ClearType", 'c', I(FQ_CLEARTYPE),
    null
  );

  s = ctrl_getset(b, "Font", "codepage", "Codepage");
  ctrl_combobox(s, null, '\0', 100, P(0), codepage_handler, P(null), P(null));

 /*
  * The Keys panel.
  */
  ctrl_settitle(b, "Keys", "Keys");

  s = ctrl_getset(b, "Keys", "keycodes", "Key codes");
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Backspace", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offcfg(backspace_sends_del)),
    "^H", 'h', I(0),
    "^?", '?', I(1),
    null
  )->column = 0;
  ctrl_radiobuttons(
    s, "Escape", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offcfg(escape_sends_fs)),
    "^[", '[', I(0),
    "^\\", '\\', I(1),
    null
  )->column = 1;

  s = ctrl_getset(b, "Keys", "alt", null);
  ctrl_checkbox(
    s, "Alt key on its own sends ^[", 'k', P(0),
    dlg_stdcheckbox_handler, I(offcfg(alt_sends_esc))
  );

  s = ctrl_getset(b, "Keys", "scrollmod", "Modifier key for scrolling");
  ctrl_radiobuttons(
    s, null, '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(scroll_mod)),
    "Shift", 's', I(SHIFT),
    "Ctrl", 'c', I(CTRL),
    "Alt", 'a', I(ALT),
    null
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
  ctrl_checkbox(
    s, "Copy on select", 'y', P(0),
    dlg_stdcheckbox_handler, I(offcfg(copy_on_select))
  );
  ctrl_checkbox(
    s, "Clicks move command line cursor", 'l', P(0),
    dlg_stdcheckbox_handler, I(offcfg(click_moves_cmd_cursor))
  );

  s = ctrl_getset(b, "Mouse", "mousemode", "Application mouse mode");
  ctrl_radiobuttons(
    s, "Default click target", '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offcfg(click_targets_app)),
    "Application", 'n', I(1),
    "Window", 'w', I(0),
    null
  );
  ctrl_radiobuttons(
    s, "Modifier key for overriding default", '\0', 3, P(0),
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
  {"CloseOnAltF4", offcfg(close_on_alt_f4), true},
  {"BoldAsBright", offcfg(bold_as_bright), true},
  {"AllowBlinking", offcfg(allow_blinking), true},
  {"CursorType", offcfg(cursor_type), 2},
  {"CursorBlinks", offcfg(cursor_blinks), true},
  {"FontIsBold", offcfg(font.isbold), 0},
  {"FontHeight", offcfg(font.height), 10},
  {"FontCharset", offcfg(font.charset), 0},
  {"FontQuality", offcfg(font_quality), FQ_DEFAULT},
  {"BackspaceSendsDEL", offcfg(backspace_sends_del), false},
  {"EscapeSendsFS", offcfg(escape_sends_fs), false},
  {"AltSendsESC", offcfg(alt_sends_esc), false},
  {"ScrollMod", offcfg(scroll_mod), SHIFT},
  {"RightClickAction", offcfg(right_click_action), RC_SHOWMENU},
  {"CopyOnSelect", offcfg(copy_on_select), false},
  {"ClickMovesCmdCursor", offcfg(click_moves_cmd_cursor), true},
  {"ClickTargetsApp", offcfg(click_targets_app), true},
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
  {"ForegroundColour", offcfg(fg_colour), {191, 191, 191}},
  {"BackgroundColour", offcfg(bg_colour), {0, 0, 0}},
  {"CursorColour", offcfg(cursor_colour), {191, 191, 191}},
};

void
load_config(void)
{
  open_settings_r(config_filename);
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
  char *errmsg = open_settings_w(config_filename);
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

