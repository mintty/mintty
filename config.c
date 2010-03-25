// config.c (part of mintty)
// Copyright 2008-10 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "config.h"
#include "ctrls.h"
#include "print.h"
#include "charset.h"
#include "term.h"
#include "win.h"

#include <sys/cygwin.h>

const char *log_file = 0;
bool utmp_enabled = false;
hold_t hold = HOLD_NEVER;

#if CYGWIN_VERSION_API_MINOR >= 222
static wchar *rc_filename = 0;
#else
static char *rc_filename = 0;
#endif

config new_cfg;
config cfg = {
  // Looks
  .fg_colour = 0xBFBFBF,
  .bg_colour = 0x000000,
  .cursor_colour = 0xBFBFBF,
  .use_system_colours = false,
  .transparency = 0,
  .opaque_when_focused = false,
  .cursor_type = CUR_LINE,
  .cursor_blinks = true,
  // Text
  .font = {.name = "Lucida Console", .isbold = false, .size = 10},
  .font_quality = FQ_DEFAULT,
  .bold_as_bright = true,
  .allow_blinking = false,
  .locale = "",
  .charset = "",
  // Keys
  .backspace_sends_bs = false,
  .alt_sends_esc = false,
  .ctrl_alt_is_altgr = false,
  .window_shortcuts = true,
  .zoom_shortcuts = true,
  .scroll_mod = MDK_SHIFT,
  // Mouse
  .copy_on_select = false,
  .clicks_place_cursor = false,
  .right_click_action = RC_SHOWMENU,
  .clicks_target_app = true,
  .click_target_mod = MDK_SHIFT,
  // Output
  .printer = "",
  .bell_sound = false,
  .bell_flash = false,
  .bell_taskbar = true,
  .term = "xterm",
  .answerback = "",
  // Window
  .cols = 80,
  .rows = 24,
  .scrollbar = true,
  .alt_screen_scroll = false,
  .scrollback_lines = 10000,
  .confirm_exit = true,
  // Hidden
  .col_spacing = 0,
  .row_spacing = 0
};

#define offcfg(option) offsetof(config, option)
#define cfg_field(option) sizeof(cfg.option), offcfg(option)

typedef enum { OPT_BOOL, OPT_INT, OPT_STRING, OPT_COLOUR } opt_type;

static const struct {
  const char *name;
  uchar type;
  uchar size;
  ushort offset;
}
options[] = {
  // Looks
  {"ForegroundColour", OPT_COLOUR, cfg_field(fg_colour)},
  {"BackgroundColour", OPT_COLOUR, cfg_field(bg_colour)},
  {"CursorColour", OPT_COLOUR, cfg_field(cursor_colour)},
  {"UseSystemColours", OPT_BOOL, cfg_field(use_system_colours)},
  {"Transparency", OPT_INT, cfg_field(transparency)},
  {"OpaqueWhenFocused", OPT_BOOL, cfg_field(opaque_when_focused)},
  {"CursorType", OPT_INT, cfg_field(cursor_type)},
  {"CursorBlinks", OPT_BOOL, cfg_field(cursor_blinks)},
  // Text
  {"Font", OPT_STRING, cfg_field(font.name)},
  {"FontIsBold", OPT_BOOL, cfg_field(font.isbold)},
  {"FontHeight", OPT_INT, cfg_field(font.size)},
  {"FontQuality", OPT_INT, cfg_field(font_quality)},
  {"BoldAsBright", OPT_BOOL, cfg_field(bold_as_bright)},
  {"AllowBlinking", OPT_BOOL, cfg_field(allow_blinking)},
  {"Locale", OPT_STRING, cfg_field(locale)},
  {"Charset", OPT_STRING, cfg_field(charset)},
  // Keys
  {"BackspaceSendsBS", OPT_BOOL, cfg_field(backspace_sends_bs)},
  {"AltSendsESC", OPT_BOOL, cfg_field(alt_sends_esc)},
  {"CtrlAltIsAltGr", OPT_BOOL, cfg_field(ctrl_alt_is_altgr)},
  {"WindowShortcuts", OPT_BOOL, cfg_field(window_shortcuts)},
  {"ZoomShortcuts", OPT_BOOL, cfg_field(zoom_shortcuts)},
  {"ScrollMod", OPT_INT, cfg_field(scroll_mod)},
  // Mouse
  {"CopyOnSelect", OPT_BOOL, cfg_field(copy_on_select)},
  {"ClicksPlaceCursor", OPT_BOOL, cfg_field(clicks_place_cursor)},
  {"RightClickAction", OPT_INT, cfg_field(right_click_action)},
  {"ClicksTargetApp", OPT_INT, cfg_field(clicks_target_app)},
  {"ClickTargetMod", OPT_INT, cfg_field(click_target_mod)},
  // Output
  {"Printer", OPT_STRING, cfg_field(printer)},
  {"BellSound", OPT_BOOL, cfg_field(bell_sound)},
  {"BellFlash", OPT_BOOL, cfg_field(bell_flash)},
  {"BellTaskbar", OPT_BOOL, cfg_field(bell_taskbar)},
  {"Term", OPT_STRING, cfg_field(term)},
  {"Answerback", OPT_STRING, cfg_field(answerback)},
  // Window
  {"Columns", OPT_INT, cfg_field(cols)},
  {"Rows", OPT_INT, cfg_field(rows)},
  {"Scrollbar", OPT_BOOL, cfg_field(scrollbar)},
  {"AltScreenScroll", OPT_BOOL, cfg_field(alt_screen_scroll)},
  {"ScrollbackLines", OPT_INT, cfg_field(scrollback_lines)},
  {"ConfirmExit", OPT_BOOL, cfg_field(confirm_exit)},
  // Hidden
  {"ColSpacing", OPT_INT, cfg_field(col_spacing)},
  {"RowSpacing", OPT_INT, cfg_field(row_spacing)},
};

static uchar option_order[lengthof(options)];
static uint option_order_len;

static int
find_option(char *name)
{
  for (uint i = 0; i < lengthof(options); i++) {
    if (!strcasecmp(name, options[i].name))
      return i;
  }
  return -1;
}

int
parse_option(char *option)
{
  char *eq= strchr(option, '=');
  if (!eq)
    return -1;
  
  uint name_len = eq - option;
  char name[name_len + 1];
  memcpy(name, option, name_len);
  name[name_len] = 0;
  
  int i = find_option(name);
  if (i < 0)
    return i;
  
  char *val = eq + 1;
  uint offset = options[i].offset;
  switch (options[i].type) {
    when OPT_BOOL:
      atoffset(bool, &cfg, offset) = atoi(val);
    when OPT_INT:
      atoffset(int, &cfg, offset) = atoi(val);
    when OPT_STRING: {
      char *str = &atoffset(char, &cfg, offset);
      uint size = options[i].size;
      strncpy(str, val, size - 1);
      str[size - 1] = 0;
    }
    when OPT_COLOUR: {
      uint r, g, b;
      if (sscanf(val, "%u,%u,%u", &r, &g, &b) == 3)
        atoffset(colour, &cfg, offset) = make_colour(r, g, b);
    }
  }
  return i;
}

void
load_config(char *filename)
{
  option_order_len = 0;

  free(rc_filename);
#if CYGWIN_VERSION_API_MINOR >= 222
  rc_filename = cygwin_create_path(CCP_POSIX_TO_WIN_W, filename);
#else
  rc_filename = strdup(filename);
#endif

  FILE *file = fopen(filename, "r");
  if (file) {
    char *line;
    size_t len;
    while (line = 0, __getline(&line, &len, file) != -1) {
      line[strcspn(line, "\r\n")] = 0;  /* trim newline */
      int i = parse_option(line);
      if (i >= 0 && !memchr(option_order, i, option_order_len))
        option_order[option_order_len++] = i;
      free(line);
    }
    fclose(file);
  }
}

static void
save_config(void)
{
#if CYGWIN_VERSION_API_MINOR >= 222
  char *filename = cygwin_create_path(CCP_WIN_W_TO_POSIX, rc_filename);
  FILE *file = fopen(filename, "w");
  free(filename);
#else
  FILE *file = fopen(rc_filename, "w");
#endif

  if (!file)
    return;

  for (uint j = 0; j < option_order_len; j++) {
    uint i = option_order[j];
    fprintf(file, "%s=", options[i].name);
    uint offset = options[i].offset;
    switch (options[i].type) {
      when OPT_BOOL:
        fprintf(file, "%i\n", atoffset(bool, &cfg, offset));
      when OPT_INT:
        fprintf(file, "%i\n", atoffset(int, &cfg, offset));
      when OPT_STRING:
        fprintf(file, "%s\n", &atoffset(char, &cfg, offset));
      when OPT_COLOUR: {
        colour c = atoffset(colour, &cfg, offset);
        fprintf(file, "%u,%u,%u\n", red(c), green(c), blue(c));
      }
    }
  }
  
  fclose(file);
  return;
}


static control *cols_box, *rows_box, *locale_box, *charset_box;

static void
apply_config(void)
{
  // Record what's changed
  for (uint i = 0; i < lengthof(options); i++) {
    uint offset = options[i].offset, size = options[i].size;
    if (memcmp((char *)&cfg + offset, (char *)&new_cfg + offset, size) &&
        !memchr(option_order, i, option_order_len))
      option_order[option_order_len++] = i;
  }
  
  win_reconfig();
  save_config();
}

static void
ok_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    apply_config();
    dlg_end();
  }
}

static void
cancel_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    dlg_end();
}

static void
apply_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    apply_config();
}

static void
about_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION)
    win_show_about();
}

static void
current_size_handler(control *unused(ctrl), void *unused(data), int event)
{
  if (event == EVENT_ACTION) {
    new_cfg.cols = term.cols;
    new_cfg.rows = term.rows;
    dlg_refresh(cols_box);
    dlg_refresh(rows_box);
  }
}

const char PRINTER_DISABLED_STRING[] = "None (printing disabled)";

static void
printerbox_handler(control *ctrl, void *unused(data), int event)
{
  if (event == EVENT_REFRESH) {
    dlg_update_start(ctrl);
    dlg_listbox_clear(ctrl);
    dlg_listbox_add(ctrl, PRINTER_DISABLED_STRING);
    uint num = printer_start_enum();
    for (uint i = 0; i < num; i++)
      dlg_listbox_add(ctrl, printer_get_name(i));
    printer_finish_enum();
    dlg_editbox_set(
      ctrl, *new_cfg.printer ? new_cfg.printer : PRINTER_DISABLED_STRING
    );
    dlg_update_done(ctrl);
  }
  else if (event == EVENT_VALCHANGE || event == EVENT_SELCHANGE) {
    dlg_editbox_get(ctrl, new_cfg.printer, sizeof (cfg.printer));
    if (strcmp(new_cfg.printer, PRINTER_DISABLED_STRING) == 0)
      *new_cfg.printer = '\0';
  }
}

static void
update_charset(void)
{
  dlg_editbox_set(charset_box, *new_cfg.locale ? new_cfg.charset : "");
}

static void
update_locale(void)
{
  if (*new_cfg.charset && !*new_cfg.locale) {
    strcpy(new_cfg.locale, "C");
    dlg_editbox_set(locale_box, new_cfg.locale);
  }
}

static void
locale_handler(control *ctrl, void *unused(data), int event)
{
  char *locale = new_cfg.locale;
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl);
      dlg_listbox_clear(ctrl);
      const char *l;
      for (int i = 0; (l = locale_menu[i]); i++)
        dlg_listbox_add(ctrl, l);
      dlg_update_done(ctrl);
      dlg_editbox_set(ctrl, locale);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, locale);
      update_charset();
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, locale, sizeof cfg.locale);
      if (event == EVENT_SELCHANGE) {
        if (*locale == '(')
          *locale = 0;
        update_charset();
      }
  }
}

static void
charset_handler(control *ctrl, void *unused(data), int event)
{
  char *charset = new_cfg.charset;
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl);
      dlg_listbox_clear(ctrl);
      const char *cs;
      for (int i = 0; (cs = charset_menu[i]); i++)
        dlg_listbox_add(ctrl, cs);
      dlg_update_done(ctrl);
      update_charset();
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, charset);
      update_locale();
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, charset, sizeof cfg.charset);
      if (event == EVENT_SELCHANGE) {
        if (*charset == '(')
          *charset = 0;
        else 
          *strchr(charset, ' ') = 0;
        update_locale();
      }
  }
}

static void
colour_handler(control *ctrl, void *unused(data), int event)
{
  colour *colour_p = ctrl->context.p;
  if (event == EVENT_ACTION) {
   /*
    * Start a colour selector, which will send us an
    * EVENT_CALLBACK when it's finished and allow us to
    * pick up the results.
    */
    dlg_coloursel_start(*colour_p);
  }
  else if (event == EVENT_CALLBACK) {
   /*
    * Collect the results of the colour selector. Will
    * return nonzero on success, or zero if the colour
    * selector did nothing (user hit Cancel, for example).
    */
    colour result;
    if (dlg_coloursel_results(&result))
      *colour_p = result;
  }
}

static void
term_handler(control *ctrl, void *unused(data), int event)
{
  switch (event) {
    when EVENT_REFRESH:
      dlg_update_start(ctrl);
      dlg_listbox_clear(ctrl);
      dlg_listbox_add(ctrl, "xterm");
      dlg_listbox_add(ctrl, "xterm-256color");
      dlg_listbox_add(ctrl, "vt100");
      dlg_update_done(ctrl);
      dlg_editbox_set(ctrl, new_cfg.term);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, new_cfg.term, sizeof cfg.term);
  }
}

static void
int_handler(control *ctrl, void *data, int event)
{
  int offset = ctrl->context.i;
  int limit = ctrl->editbox.context2.i;
  int *field = &atoffset(int, data, offset);
  char buf[16];
  switch (event) {
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, buf, lengthof(buf));
      *field = max(0, min(atoi(buf), limit));
    when EVENT_REFRESH:
      sprintf(buf, "%i", *field);
      dlg_editbox_set(ctrl, buf);
  }
}

static void
string_handler(control *ctrl, void *data, int event)
{
  int offset = ctrl->context.i;
  int size = ctrl->editbox.context2.i;
  char *buf = &atoffset(char, data, offset);
  switch (event) {
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, buf, size);
    when EVENT_REFRESH:
      dlg_editbox_set(ctrl, buf);
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
    s, "Access scrollback from alternate screen", 'a', P(0),
    dlg_stdcheckbox_handler, I(offcfg(alt_screen_scroll))
  );
  ctrl_columns(s, 2, 53, 47);
  ctrl_editbox(
    s, "Scrollback lines", 'b', 38, P(0),
    int_handler, I(offcfg(scrollback_lines)), I(1000000)
  )->column = 0;

  s = ctrl_getset(b, "Window", "options", null);
  ctrl_checkbox(
    s, "Ask for exit confirmation", 'x', P(0),
    dlg_stdcheckbox_handler, I(offcfg(confirm_exit))
  );
}
