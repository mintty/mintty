// config.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"

#include "ctrls.h"
#include "print.h"
#include "unicode.h"
#include "term.h"

/*
 * config.c - the configuration box.
 */

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
    strcpy(cfg.line_codepage, cp_name(decode_codepage(cfg.line_codepage)));
    dlg_listbox_clear(ctrl, dlg);
    for (i = 0; (cp = cp_enumerate(i)) != null; i++)
      dlg_listbox_add(ctrl, dlg, cp);
    dlg_editbox_set(ctrl, dlg, cfg.line_codepage);
    dlg_update_done(ctrl, dlg);
  }
  else if (event == EVENT_VALCHANGE) {
    dlg_editbox_get(ctrl, dlg, cfg.line_codepage, sizeof (cfg.line_codepage));
    strcpy(cfg.line_codepage, cp_name(decode_codepage(cfg.line_codepage)));
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
  c = ctrl_pushbutton(s, "OK", 'o', P(0), ok_handler, P(0));
  c->button.isdefault = true;
  c->column = 3;
  c = ctrl_pushbutton(s, "Cancel", 'c', P(0), cancel_handler, P(0));
  c->button.iscancel = true;
  c->column = 4;

 /*
  * The Window panel.
  */
  ctrl_settitle(b, "Window", "Window");

  s = ctrl_getset(b, "Window", "size", "Size");
  ctrl_columns(s, 2, 50, 50);
  ctrl_editbox(
    s, "Columns", '\0', 100, P(0),
    dlg_stdeditbox_handler, I(offsetof(config, cols)), I(-1)
  )->column = 0;
  ctrl_editbox(
    s, "Rows", '\0', 100, P(0),
    dlg_stdeditbox_handler, I(offsetof(config, rows)), I(-1)
  )->column = 1;

  s = ctrl_getset(b, "Window", "other", null);
  ctrl_checkbox(
    s, "Show scrollbar", '\0', P(0),
    dlg_stdcheckbox_handler, I(offsetof(config, scrollbar))
  );

  s = ctrl_getset(b, "Window", "trans", "Transparency");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, transparency)),
    "Off", '\0', I(0),
    "Low", '\0', I(1),
    "Medium", '\0', I(2), 
    "High", '\0', I(3), 
    null
  );

 /*
  * The Screen panel.
  */
  ctrl_settitle(b, "Screen", "Screen");

  s = ctrl_getset(b, "Screen", "colours", "Colours");
  ctrl_columns(s, 3, 33, 33, 33);
  ctrl_pushbutton(
    s, "Foreground", '\0', P(0), colour_handler, P(&cfg.fg_colour)
  )->column = 0;
  ctrl_pushbutton(
    s, "Background", '\0', P(0), colour_handler, P(&cfg.bg_colour)
  )->column = 1;
  ctrl_pushbutton(
    s, "Cursor", '\0', P(0), colour_handler, P(&cfg.cursor_colour)
  )->column = 2;
  ctrl_columns(s, 1, 100);

  s = ctrl_getset(b, "Screen", "cursor", "Cursor");
  ctrl_radiobuttons(
    s, null, '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, cursor_type)),
    "Block", '\0', I(CUR_BLOCK),
    "Underline", '\0', I(CUR_UNDERLINE),
    "Line", '\0', I(CUR_LINE), 
    null
  );
  s = ctrl_getset(b, "Screen", "blinking", "Blinking");
  ctrl_checkbox(
     s, "Show blinking cursor", '\0', P(0),
     dlg_stdcheckbox_handler, I(offsetof(config, blink_cur))
  );
  ctrl_checkbox(
    s, "Allow blinking text", '\0', P(0),
    dlg_stdcheckbox_handler, I(offsetof(config, blinktext))
  );

 /*
  * The Text panel.
  */
  ctrl_settitle(b, "Text", "Text");

  s = ctrl_getset(b, "Text", "font", "Font");
  ctrl_fontsel(
    s, null, '\0', P(0), dlg_stdfontsel_handler, I(offsetof(config, font))
  );
  ctrl_checkbox(
    s, "Show bold text as bright instead", '\0', P(0), dlg_stdcheckbox_handler,
    I(offsetof(config, bold_as_bright))
  );

  s = ctrl_getset(b, "Text", "smooth", "Smoothing");
  ctrl_radiobuttons(
    s, null, 's', 2, P(0), dlg_stdradiobutton_handler, 
    I(offsetof(config, font_quality)),
    "System Default", I(FQ_DEFAULT),
    "Antialiased", I(FQ_ANTIALIASED),
    "Non-Antialiased", I(FQ_NONANTIALIASED),
    "ClearType", I(FQ_CLEARTYPE),
    null
  );

  s = ctrl_getset(b, "Text", "codepage", "Codepage");
  ctrl_combobox(s, null, '\0', 100, P(0), codepage_handler, P(null), P(null));

 /*
  * The Input panel.
  */
  ctrl_settitle(b, "Input", "Input");

  s = ctrl_getset(b, "Input", "scrollmod", "Modifier key for scrolling");
  ctrl_radiobuttons(
    s, null, '\0', 3, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, scroll_mod)),
    "Shift", '\0', I(SHIFT),
    "Alt", '\0', I(ALT),
    "Ctrl", '\0', I(CTRL),
    null
  );

  s = ctrl_getset(b, "Input", "keys", "Key codes");
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Backspace", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, backspace_sends_del)),
    "^H", '\0', I(0),
    "^?", '\0', I(1),
    null
  )->column = 1;
  ctrl_radiobuttons(
    s, "Escape", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, escape_sends_fs)),
    "^[", '\0', I(0),
    "^\\", '\0', I(1),
    null
  )->column = 0;

  s = ctrl_getset(b, "Input", "options", null);
  ctrl_checkbox(
    s, "Alt key on its own sends ^[", '\0', P(0),
    dlg_stdcheckbox_handler, I(offsetof(config, alt_sends_esc))
  );
  ctrl_checkbox(
    s, "Copy on select", '\0', P(0),
    dlg_stdcheckbox_handler, I(offsetof(config, copy_on_select))
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
    s, "Action:", '\0', 1, P(0), dlg_stdradiobutton_handler, 
    I(offsetof(config, bell)),
    "None", '\0', I(BELL_DISABLED),
    "System sound", '\0', I(BELL_SOUND),
    "Flash window", '\0', I(BELL_VISUAL),
    null
  )->column = 0;
  ctrl_radiobuttons(
    s, "Taskbar indication:", '\0', 1, P(0), dlg_stdradiobutton_handler,
    I(offsetof(config, bell_ind)),
    "Disabled", '\0', I(B_IND_DISABLED),
    "Flashing", '\0', I(B_IND_FLASH),
    "Steady", '\0', I(B_IND_STEADY),
    null
  )->column = 1;
}

char *
save_config(void)
{
  char *errmsg = open_settings_w();
  if (errmsg)
    return errmsg;
  write_int_setting("CopyOnSelect", cfg.copy_on_select);
  write_int_setting("EscapeSendsFS", cfg.escape_sends_fs);
  write_int_setting("BackspaceSendsDEL", cfg.backspace_sends_del);
  write_int_setting("AltSendsESC", cfg.alt_sends_esc);
  write_int_setting("CurType", cfg.cursor_type);
  write_int_setting("BlinkCur", cfg.blink_cur);
  write_int_setting("Bell", cfg.bell);
  write_int_setting("BellInd", cfg.bell_ind);
  write_int_setting("Columns", cfg.cols);
  write_int_setting("Rows", cfg.rows);
  write_font_setting("Font", cfg.font);
  write_int_setting("FontQuality", cfg.font_quality);
  write_int_setting("BoldAsBright", cfg.bold_as_bright);
  write_colour_setting("ForegroundColour", cfg.fg_colour);
  write_colour_setting("BackgroundColour", cfg.bg_colour);
  write_colour_setting("CursorColour", cfg.cursor_colour);
  write_int_setting("Transparency", cfg.transparency);
  write_string_setting("LineCodePage", cfg.line_codepage);
  write_string_setting("Printer", cfg.printer);
  write_int_setting("Scrollbar", cfg.scrollbar);
  write_int_setting("ScrollMod", cfg.scroll_mod);
  write_int_setting("BlinkText", cfg.blinktext);
  close_settings_w();
  return 0;
}

void
load_config(void)
{
  open_settings_r();
  read_int_setting("CopyOnSelect", false, &cfg.copy_on_select);
  read_int_setting("EscapeSendsFS", false, &cfg.escape_sends_fs);
  read_int_setting("BackspaceSendsDEL", false, &cfg.backspace_sends_del);
  read_int_setting("AltSendsESC", false, &cfg.alt_sends_esc);
  read_int_setting("CurType", 2, &cfg.cursor_type);
  read_int_setting("BlinkCur", true, &cfg.blink_cur);
  read_int_setting("Bell", BELL_SOUND, &cfg.bell);
  read_int_setting("BellInd", B_IND_STEADY, &cfg.bell_ind);
  read_int_setting("Columns", 80, &cfg.cols);
  read_int_setting("Rows", 24, &cfg.rows);
  read_font_setting("Font", (font_spec){"Lucida Console\0", false, 9, 0},
                    &cfg.font);
  read_int_setting("FontQuality", FQ_DEFAULT, &cfg.font_quality);
  read_int_setting("BoldAsBright", true, &cfg.bold_as_bright);
  read_colour_setting("ForegroundColour", (colour){191, 191, 191},
                      &cfg.fg_colour);
  read_colour_setting("BackgroundColour", (colour){0, 0, 0},
                      &cfg.bg_colour);
  read_colour_setting("CursorColour", (colour){191, 191, 191},
                      &cfg.cursor_colour);
  read_int_setting("Transparency", 0, &cfg.transparency);
 /*
  * The empty default for LineCodePage will be converted later
  * into a plausible default for the locale.
  */
  read_string_setting("LineCodePage", "", cfg.line_codepage,
                                          sizeof cfg.line_codepage);
  read_string_setting("Printer", "", cfg.printer, sizeof cfg.printer);
  read_int_setting("Scrollbar", true, &cfg.scrollbar);
  read_int_setting("ScrollMod", SHIFT, &cfg.scroll_mod);
  read_int_setting("BlinkText", true, &cfg.blinktext);
  close_settings_r();
}
