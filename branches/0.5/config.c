// config.c (part of mintty)
// Copyright 2008-09 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"

#include "ctrls.h"
#include "print.h"
#include "charset.h"
#include "term.h"
#include "win.h"

const char *log_file = 0;
bool utmp_enabled = false;
hold_t hold = HOLD_NEVER;

const char *config_file = 0;
config cfg, new_cfg;

static control *cols_box, *rows_box, *locale_box, *charset_box;

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
    new_cfg.cols = term.cols;
    new_cfg.rows = term.rows;
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
update_charset(void *dlg)
{
  dlg_editbox_set(charset_box, dlg, *new_cfg.locale ? new_cfg.charset : "");
}

static void
update_locale(void *dlg)
{
  if (*new_cfg.charset && !*new_cfg.locale) {
    strcpy(new_cfg.locale, "C");
    dlg_editbox_set(locale_box, dlg, new_cfg.locale);
  }
}

static void
locale_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  char *locale = new_cfg.locale;
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl, dlg);
      dlg_listbox_clear(ctrl, dlg);
      const char *l;
      for (int i = 0; (l = locale_menu[i]); i++)
        dlg_listbox_add(ctrl, dlg, l);
      dlg_update_done(ctrl, dlg);
      dlg_editbox_set(ctrl, dlg, locale);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, dlg, locale);
      update_charset(dlg);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, dlg, locale, sizeof cfg.locale);
      correct_locale(locale);
      if (event == EVENT_SELCHANGE)
        update_charset(dlg);
  }
}

static void
charset_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  char *charset = new_cfg.charset;
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl, dlg);
      dlg_listbox_clear(ctrl, dlg);
      const char *cs;
      for (int i = 0; (cs = charset_menu[i]); i++)
        dlg_listbox_add(ctrl, dlg, cs);
      dlg_update_done(ctrl, dlg);
      update_charset(dlg);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, dlg, charset);
      update_locale(dlg);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, dlg, charset, sizeof cfg.charset);
      correct_charset(charset);
      if (event == EVENT_SELCHANGE)
        update_locale(dlg);
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

static void
term_handler(control *ctrl, void *dlg, void *unused(data), int event)
{
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl, dlg);
      dlg_listbox_clear(ctrl, dlg);
      dlg_listbox_add(ctrl, dlg, "xterm");
      dlg_listbox_add(ctrl, dlg, "xterm-256color");
      dlg_listbox_add(ctrl, dlg, "vt100");
      dlg_update_done(ctrl, dlg);
      dlg_editbox_set(ctrl, dlg, new_cfg.term);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, dlg, new_cfg.term, sizeof cfg.term);
  }
}

static void
int_handler(control *ctrl, void *dlg, void *data, int event)
{
  int offset = ctrl->context.i;
  int limit = ctrl->editbox.context2.i;
  int *field = &atoffset(int, data, offset);
  char buf[16];
  switch (event) {
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, dlg, buf, lengthof(buf));
      *field = max(0, min(atoi(buf), limit));
    when EVENT_REFRESH:
      sprintf(buf, "%i", *field);
      dlg_editbox_set(ctrl, dlg, buf);
  }
}

static void
string_handler(control *ctrl, void *dlg, void *data, int event)
{
  int offset = ctrl->context.i;
  int size = ctrl->editbox.context2.i;
  char *buf = &atoffset(char, data, offset);
  switch (event) {
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, dlg, buf, size);
    when EVENT_REFRESH:
      dlg_editbox_set(ctrl, dlg, buf);
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
  //ctrl_columns(s, 1, 100);
  ctrl_checkbox(
    s, "Use system colours instead", 's', P(0),
    dlg_stdcheckbox_handler, I(offcfg(use_system_colours))
  );
  
  s = ctrl_getset(b, "Looks", "trans", "Transparency");
  bool with_glass = win_is_glass_available();
  ctrl_radiobuttons(
    s, null, '\0', 4 + with_glass, P(0), dlg_stdradiobutton_handler,
    I(offcfg(transparency)),
    "Off", 'o', I(0),
    "Low", 'l', I(1),
    with_glass ? "Med." : "Medium", 'm', I(2), 
    "High", 'h', I(3), 
    with_glass ? "Glass" : null, 'g', I(-1), 
    null
  );
  ctrl_checkbox(
    s, "Opaque when focused", 'p', P(0),
    dlg_stdcheckbox_handler, I(offcfg(opaque_when_focused))
  );

  s = ctrl_getset(b, "Looks", "curtype", "Cursor");
  ctrl_columns(s, 2, 80, 20);
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(cursor_type)),
    "Line", 'n', I(CUR_LINE), 
    "Block", 'k', I(CUR_BLOCK),
    "Underscore", 'u', I(CUR_UNDERSCORE),
    null
  )->column = 0;
  ctrl_checkbox(
    s, "Blink", 'e', P(0), dlg_stdcheckbox_handler, I(offcfg(cursor_blinks))
  )->column = 1;

 /*
  * The Text panel.
  */
  ctrl_settitle(b, "Text", "Text");

  s = ctrl_getset(b, "Text", "font", "Font");
  ctrl_fontsel(
    s, null, '\0', P(0), dlg_stdfontsel_handler, I(offcfg(font))
  );
  ctrl_radiobuttons(
    s, "Smoothing", '\0', 4, P(0), dlg_stdradiobutton_handler, 
    I(offcfg(font_quality)),
    "Default", 'd', I(FQ_DEFAULT),
    "None", 'n', I(FQ_NONANTIALIASED),
    "Partial", 'p', I(FQ_ANTIALIASED),
    "Full", 'f', I(FQ_CLEARTYPE),
    null
  );

  s = ctrl_getset(b, "Text", "effects", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Show bold as bright", 'b', P(0), dlg_stdcheckbox_handler,
    I(offcfg(bold_as_bright))
  )->column = 0;
  ctrl_checkbox(
    s, "Allow blinking", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(allow_blinking))
  )->column = 1;

  s = ctrl_getset(b, "Text", "locale", null);
  ctrl_columns(s, 2, 29, 71);
  (locale_box = ctrl_combobox(
    s, "Locale", 'l', 100, P(0), locale_handler, P(0), P(0)
  ))->column = 0;
  (charset_box = ctrl_combobox(
    s, "Character set", 'c', 100, P(0), charset_handler, P(0), P(0)
  ))->column = 1;

 /*
  * The Keys panel.
  */
  ctrl_settitle(b, "Keys", "Keys");
  
  s = ctrl_getset(b, "Keys", "keys", null);
  ctrl_checkbox(
    s, "Backspace sends ^H", 'b', P(0),
    dlg_stdcheckbox_handler, I(offcfg(backspace_sends_bs))
  );
  ctrl_checkbox(
    s, "Lone Alt sends ESC", 'l', P(0),
    dlg_stdcheckbox_handler, I(offcfg(alt_sends_esc))
  );
  ctrl_checkbox(
    s, "Ctrl+LeftAlt is AltGr", 'g', P(0),
    dlg_stdcheckbox_handler, I(offcfg(ctrl_alt_is_altgr))
  );

  s = ctrl_getset(b, "Keys", "shortcuts", "Shortcuts");
  ctrl_checkbox(
    s, "Menu and fullscreen (Alt+Space/Enter)", 'm', P(0),
    dlg_stdcheckbox_handler, I(offcfg(window_shortcuts))
  );
  ctrl_checkbox(
    s, "Zoom (Ctrl+plus/minus/zero)", 'z', P(0),
    dlg_stdcheckbox_handler, I(offcfg(zoom_shortcuts))
  );
  
  s = ctrl_getset(b, "Keys", "scroll", "Modifier for scrolling");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0),      
    dlg_stdradiobutton_handler, I(offcfg(scroll_mod)),
    "Shift", 's', I(MDK_SHIFT),
    "Ctrl", 'c', I(MDK_CTRL),
    "Alt", 'a', I(MDK_ALT),
    "Off", 'o', I(0),
    null
  );

 /*
  * The Mouse panel.
  */
  ctrl_settitle(b, "Mouse", "Mouse");

  s = ctrl_getset(b, "Mouse", "mouseopts", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Copy on select", 'y', P(0),
    dlg_stdcheckbox_handler, I(offcfg(copy_on_select))
  )->column = 0;
  ctrl_checkbox(
    s, "Clicks place cursor", 'k', P(0),
    dlg_stdcheckbox_handler, I(offcfg(clicks_place_cursor))
  )->column = 1;

  s = ctrl_getset(b, "Mouse", "rightclick", "Right click action");
  ctrl_radiobuttons(
    s, null, '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(right_click_action)),
    "Paste", 'p', I(RC_PASTE),
    "Extend", 'x', I(RC_EXTEND),
    "Show menu", 'm', I(RC_SHOWMENU),
    null
  );
  
  s = ctrl_getset(b, "Mouse", "mousemode", "Application mouse mode");
  ctrl_radiobuttons(
    s, "Default click target", '\0', 4, P(0), dlg_stdradiobutton_handler,
    I(offcfg(clicks_target_app)),
    "Window", 'w', I(0),
    "Application", 'n', I(1),
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for overriding default", '\0', 4, P(0),
    dlg_stdradiobutton_handler, I(offcfg(click_target_mod)),
    "Shift", 's', I(MDK_SHIFT),
    "Ctrl", 'c', I(MDK_CTRL),
    "Alt", 'a', I(MDK_ALT),
    "Off", 'o', I(0),
    null
  );
  
 /*
  * The Output panel.
  */
  ctrl_settitle(b, "Output", "Output");

  s = ctrl_getset(b, "Output", "printer", "Printer");
  ctrl_combobox(
    s, null, '\0', 100, P(0), printerbox_handler, P(0), P(0)
  );

  s = ctrl_getset(b, "Output", "bell", "Bell");
  ctrl_checkbox(
    s, "Play sound", 'p', P(0),
    dlg_stdcheckbox_handler, I(offcfg(bell_sound))
  );
  ctrl_checkbox(
    s, "Flash screen", 'f', P(0),
    dlg_stdcheckbox_handler, I(offcfg(bell_flash))
  );
  ctrl_checkbox(
    s, "Highlight in taskbar", 'h', P(0),
    dlg_stdcheckbox_handler, I(offcfg(bell_taskbar))
  );

  s = ctrl_getset(b, "Output", "ids", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_combobox(
    s, "TERM (at startup)", 't', 100, P(0), term_handler, P(0), P(0)
  )->column = 0;
  ctrl_editbox(
    s, "Answerback", 'a', 100, P(0),
    string_handler, I(offcfg(answerback)), I(sizeof cfg.answerback)
  )->column = 1;

 /*
  * The Window panel.
  */
  ctrl_settitle(b, "Window", "Window");

  s = ctrl_getset(b, "Window", "size", "Default size");
  ctrl_columns(s, 5, 35, 3, 28, 4, 30);
  (cols_box = ctrl_editbox(
    s, "Columns", 'c', 44, P(0), int_handler, I(offcfg(cols)), I(256)
  ))->column = 0;
  (rows_box = ctrl_editbox(
    s, "Rows", 'r', 55, P(0), int_handler, I(offcfg(rows)), I(256)
  ))->column = 2;
  ctrl_pushbutton(
    s, "Current size", 'u', P(0), current_size_handler, P(0)
  )->column = 4;

  s = ctrl_getset(b, "Window", "scroll", "Scrolling");
  ctrl_checkbox(
    s, "Show scrollbar", 's', P(0),
    dlg_stdcheckbox_handler, I(offcfg(scrollbar))
  );
  ctrl_checkbox(
    s, "Enable scrollback on alternate screen", 'e', P(0),
    dlg_stdcheckbox_handler, I(offcfg(alt_screen_scroll))
  );
  ctrl_columns(s, 2, 53, 47);
  ctrl_editbox(
    s, "Scrollback lines", 'b', 38, P(0),
    int_handler, I(offsetof(config, scrollback_lines)), I(1000000)
  )->column = 0;

  s = ctrl_getset(b, "Window", "options", null);
  ctrl_checkbox(
    s, "Ask for exit confirmation", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(confirm_exit))
  );
}


typedef const struct {
  const char *key;
  ushort offset;
  ushort def;
} int_setting;

static const int_setting
int_settings[] = {
  {"Columns", offcfg(cols), 80},
  {"Rows", offcfg(rows), 24},
  {"Transparency", offcfg(transparency), 0},
  {"OpaqueWhenFocused", offcfg(opaque_when_focused), 0},
  {"Scrollbar", offcfg(scrollbar), true},
  {"ScrollbackLines", offcfg(scrollback_lines), 10000},
  {"AltScreenScroll", offcfg(alt_screen_scroll), false},
  {"ConfirmExit", offcfg(confirm_exit), true},
  {"CtrlAltIsAltGr", offcfg(ctrl_alt_is_altgr), false},
  {"AltSendsESC", offcfg(alt_sends_esc), false},
  {"BackspaceSendsBS", offcfg(backspace_sends_bs), false},
  {"WindowShortcuts", offcfg(window_shortcuts), true},
  {"ZoomShortcuts", offcfg(zoom_shortcuts), true},
  {"UseSystemColours", offcfg(use_system_colours), true},
  {"BoldAsBright", offcfg(bold_as_bright), true},
  {"AllowBlinking", offcfg(allow_blinking), false},
  {"CursorType", offcfg(cursor_type), 2},
  {"CursorBlinks", offcfg(cursor_blinks), true},
  {"FontIsBold", offcfg(font.isbold), 0},
  {"FontHeight", offcfg(font.size), 10},
  {"FontQuality", offcfg(font_quality), FQ_DEFAULT},
  {"ScrollMod", offcfg(scroll_mod), MDK_SHIFT},
  {"RightClickAction", offcfg(right_click_action), RC_SHOWMENU},
  {"CopyOnSelect", offcfg(copy_on_select), false},
  {"ClicksPlaceCursor", offcfg(clicks_place_cursor), false},
  {"ClicksTargetApp", offcfg(clicks_target_app), true},
  {"ClickTargetMod", offcfg(click_target_mod), MDK_SHIFT},
  {"BellSound", offcfg(bell_sound), false},
  {"BellFlash", offcfg(bell_flash), false},
  {"BellTaskbar", offcfg(bell_taskbar), true},
};

typedef const struct {
  const char *key;
  ushort offset;
  ushort len;
  const char *def;
} string_setting;

static const string_setting
string_settings[] = {
  {"Font", offcfg(font.name), sizeof cfg.font.name, "Lucida Console"},
  {"Locale", offcfg(locale), sizeof cfg.locale, ""},
  {"Charset", offcfg(charset), sizeof cfg.charset, ""},
  {"Printer", offcfg(printer), sizeof cfg.printer, ""},
  {"Term", offcfg(term), sizeof cfg.term, "xterm"},
  {"Answerback", offcfg(answerback), sizeof cfg.answerback, ""},
};

typedef const struct {
  const char *key;
  ushort offset;
  colour def;
} colour_setting;

static const colour_setting
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
  
  if (!*cfg.charset) {
    read_string_setting("Codepage", cfg.charset, sizeof cfg.charset, "");
    if (*cfg.charset && !*cfg.locale)
      strcpy(cfg.locale, "C");
  }
  
  close_settings_r();
  
  correct_locale(cfg.locale);
  correct_charset(cfg.charset);
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

