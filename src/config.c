// config.c (part of mintty)
// Copyright 2008-13 Andy Koppe, 2015-2016 Thomas Wolff
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "term.h"
#include "ctrls.h"
#include "print.h"
#include "charset.h"
#include "win.h"

#include <termios.h>
#include <sys/cygwin.h>


#define dont_debug_config

#define dont_support_blurred


#if CYGWIN_VERSION_API_MINOR < 74
// needed for MinGW MSYS
#define wcscpy(tgt, src) memcpy(tgt, src, (wcslen(src) + 1) * sizeof(wchar))
#endif

#if CYGWIN_VERSION_API_MINOR >= 222
static wstring rc_filename = 0;
#else
static string rc_filename = 0;
#endif

const config default_cfg = {
  // Looks
  .fg_colour = 0xBFBFBF,
  .bold_colour = (colour)-1,
  .bg_colour = 0x000000,
  .cursor_colour = 0xBFBFBF,
  .search_fg_colour = 0x000000,
  .search_bg_colour = 0x00DDDD,
  .search_current_colour = 0x0099DD,
  .theme_file = L"",
  .transparency = 0,
  .blurred = false,
  .opaque_when_focused = false,
  .cursor_type = CUR_LINE,
  .cursor_blinks = true,
  // Text
  .font = {.name = L"Lucida Console", .size = 9, .weight = 400, .isbold = false},
  .show_hidden_fonts = false,
  .font_smoothing = FS_DEFAULT,
  .bold_as_font = -1,  // -1 means "the opposite of bold_as_colour"
  .bold_as_colour = true,
  .allow_blinking = false,
  .locale = "",
  .charset = "",
  // Keys
  .backspace_sends_bs = CERASE == '\b',
  .delete_sends_del = false,
  .ctrl_alt_is_altgr = false,
  .clip_shortcuts = true,
  .window_shortcuts = true,
  .switch_shortcuts = true,
  .zoom_shortcuts = true,
  .zoom_font_with_window = true,
  .alt_fn_shortcuts = true,
  .ctrl_shift_shortcuts = false,
  .key_prtscreen = "",	// VK_SNAPSHOT
  .key_pause = "",	// VK_PAUSE
  .key_break = "",	// VK_CANCEL
  .key_menu = "",	// VK_APPS
  .key_scrlock = "",	// VK_SCROLL
  // Mouse
  .copy_on_select = true,
  .copy_as_rtf = true,
  .clicks_place_cursor = false,
  .middle_click_action = MC_PASTE,
  .right_click_action = RC_MENU,
  .zoom_mouse = true,
  .clicks_target_app = true,
  .click_target_mod = MDK_SHIFT,
  .hide_mouse = true,
  // Window
  .cols = 80,
  .rows = 24,
  .scrollbar = 1,
  .scrollback_lines = 10000,
  .scroll_mod = MDK_SHIFT,
  .pgupdn_scroll = false,
  .search_bar = "",
  // Terminal
  .term = "xterm",
  .answerback = L"",
  .bell_sound = false,
  .bell_type = 1,
  .bell_file = L"",
  .bell_freq = 0,
  .bell_len = 400,
  .bell_flash = false,
  .bell_taskbar = true,
  .printer = L"",
  .confirm_exit = true,
  // Command line
  .class = L"",
  .hold = HOLD_START,
  .exit_write = false,
  .exit_title = L"",
  .icon = L"",
  .log = L"",
  .utmp = false,
  .title = L"",
  .daemonize = true,
  .daemonize_always = false,
  // "Hidden"
  .app_id = L"",
  .app_name = L"",
  .app_launch_cmd = L"",
  .col_spacing = 0,
  .row_spacing = 0,
  .padding = 1,
  .word_chars = "",
  .word_chars_excl = "",
  .use_system_colours = false,
  .ime_cursor_colour = DEFAULT_COLOUR,
  .ansi_colours = {
    [BLACK_I]        = 0x000000,
    [RED_I]          = 0x0000BF,
    [GREEN_I]        = 0x00BF00,
    [YELLOW_I]       = 0x00BFBF,
    [BLUE_I]         = 0xBF0000,
    [MAGENTA_I]      = 0xBF00BF,
    [CYAN_I]         = 0xBFBF00,
    [WHITE_I]        = 0xBFBFBF,
    [BOLD_BLACK_I]   = 0x404040,
    [BOLD_RED_I]     = 0x4040FF,
    [BOLD_GREEN_I]   = 0x40FF40,
    [BOLD_YELLOW_I]  = 0x40FFFF,
    [BOLD_BLUE_I]    = 0xFF6060,
    [BOLD_MAGENTA_I] = 0xFF40FF,
    [BOLD_CYAN_I]    = 0xFFFF40,
    [BOLD_WHITE_I]   = 0xFFFFFF,
  }
};

config cfg, new_cfg, file_cfg;

typedef enum {
  OPT_BOOL, OPT_MOD, OPT_TRANS, OPT_CURSOR, OPT_FONTSMOOTH,
  OPT_MIDDLECLICK, OPT_RIGHTCLICK, OPT_SCROLLBAR, OPT_WINDOW, OPT_HOLD,
  OPT_INT, OPT_COLOUR, OPT_STRING, OPT_WSTRING,
  OPT_LEGACY = 16
} opt_type;

#define offcfg(option) offsetof(config, option)

static const struct {
  string name;
  uchar type;
  ushort offset;
}
options[] = {
  // Colour base options;
  // check_legacy_options() assumes these are the first three here:
  {"ForegroundColour", OPT_COLOUR, offcfg(fg_colour)},
  {"BackgroundColour", OPT_COLOUR, offcfg(bg_colour)},
  {"UseSystemColours", OPT_BOOL | OPT_LEGACY, offcfg(use_system_colours)},

  // Looks
  {"BoldColour", OPT_COLOUR, offcfg(bold_colour)},
  {"CursorColour", OPT_COLOUR, offcfg(cursor_colour)},
  {"SearchForegroundColour", OPT_COLOUR, offcfg(search_fg_colour)},
  {"SearchBackgroundColour", OPT_COLOUR, offcfg(search_bg_colour)},
  {"SearchCurrentColour", OPT_COLOUR, offcfg(search_current_colour)},
  {"ThemeFile", OPT_WSTRING, offcfg(theme_file)},
  {"Transparency", OPT_TRANS, offcfg(transparency)},
#ifdef support_blurred
  {"Blur", OPT_BOOL, offcfg(blurred)},
#endif
  {"OpaqueWhenFocused", OPT_BOOL, offcfg(opaque_when_focused)},
  {"CursorType", OPT_CURSOR, offcfg(cursor_type)},
  {"CursorBlinks", OPT_BOOL, offcfg(cursor_blinks)},

  // Text
  {"Font", OPT_WSTRING, offcfg(font.name)},
  {"FontSize", OPT_INT | OPT_LEGACY, offcfg(font.size)},
  {"FontHeight", OPT_INT, offcfg(font.size)},
  {"FontWeight", OPT_INT, offcfg(font.weight)},
  {"FontIsBold", OPT_BOOL, offcfg(font.isbold)},
  {"ShowHiddenFonts", OPT_BOOL, offcfg(show_hidden_fonts)},
  {"FontSmoothing", OPT_FONTSMOOTH, offcfg(font_smoothing)},
  {"BoldAsFont", OPT_BOOL, offcfg(bold_as_font)},
  {"BoldAsColour", OPT_BOOL, offcfg(bold_as_colour)},
  {"AllowBlinking", OPT_BOOL, offcfg(allow_blinking)},
  {"Locale", OPT_STRING, offcfg(locale)},
  {"Charset", OPT_STRING, offcfg(charset)},

  // Keys
  {"BackspaceSendsBS", OPT_BOOL, offcfg(backspace_sends_bs)},
  {"DeleteSendsDEL", OPT_BOOL, offcfg(delete_sends_del)},
  {"CtrlAltIsAltGr", OPT_BOOL, offcfg(ctrl_alt_is_altgr)},
  {"ClipShortcuts", OPT_BOOL, offcfg(clip_shortcuts)},
  {"WindowShortcuts", OPT_BOOL, offcfg(window_shortcuts)},
  {"SwitchShortcuts", OPT_BOOL, offcfg(switch_shortcuts)},
  {"ZoomShortcuts", OPT_BOOL, offcfg(zoom_shortcuts)},
  {"ZoomFontWithWindow", OPT_BOOL, offcfg(zoom_font_with_window)},
  {"AltFnShortcuts", OPT_BOOL, offcfg(alt_fn_shortcuts)},
  {"CtrlShiftShortcuts", OPT_BOOL, offcfg(ctrl_shift_shortcuts)},
  {"Key_PrintScreen", OPT_STRING, offcfg(key_prtscreen)},
  {"Key_Pause", OPT_STRING, offcfg(key_pause)},
  {"Key_Break", OPT_STRING, offcfg(key_break)},
  {"Key_Menu", OPT_STRING, offcfg(key_menu)},
  {"Key_ScrollLock", OPT_STRING, offcfg(key_scrlock)},
  {"Break", OPT_STRING | OPT_LEGACY, offcfg(key_break)},
  {"Pause", OPT_STRING | OPT_LEGACY, offcfg(key_pause)},

  // Mouse
  {"CopyOnSelect", OPT_BOOL, offcfg(copy_on_select)},
  {"CopyAsRTF", OPT_BOOL, offcfg(copy_as_rtf)},
  {"ClicksPlaceCursor", OPT_BOOL, offcfg(clicks_place_cursor)},
  {"MiddleClickAction", OPT_MIDDLECLICK, offcfg(middle_click_action)},
  {"RightClickAction", OPT_RIGHTCLICK, offcfg(right_click_action)},
  {"ZoomMouse", OPT_BOOL, offcfg(zoom_mouse)},
  {"ClicksTargetApp", OPT_BOOL, offcfg(clicks_target_app)},
  {"ClickTargetMod", OPT_MOD, offcfg(click_target_mod)},
  {"HideMouse", OPT_BOOL, offcfg(hide_mouse)},

  // Window
  {"Columns", OPT_INT, offcfg(cols)},
  {"Rows", OPT_INT, offcfg(rows)},
  {"ScrollbackLines", OPT_INT, offcfg(scrollback_lines)},
  {"Scrollbar", OPT_SCROLLBAR, offcfg(scrollbar)},
  {"ScrollMod", OPT_MOD, offcfg(scroll_mod)},
  {"PgUpDnScroll", OPT_BOOL, offcfg(pgupdn_scroll)},
  {"SearchBar", OPT_STRING, offcfg(search_bar)},

  // Terminal
  {"Term", OPT_STRING, offcfg(term)},
  {"Answerback", OPT_WSTRING, offcfg(answerback)},
  {"BellSound", OPT_BOOL, offcfg(bell_sound)},
  {"BellType", OPT_INT, offcfg(bell_type)},
  {"BellFile", OPT_WSTRING, offcfg(bell_file)},
  {"BellFreq", OPT_INT, offcfg(bell_freq)},
  {"BellLen", OPT_INT, offcfg(bell_len)},
  {"BellFlash", OPT_BOOL, offcfg(bell_flash)},
  {"BellTaskbar", OPT_BOOL, offcfg(bell_taskbar)},
  {"Printer", OPT_WSTRING, offcfg(printer)},
  {"ConfirmExit", OPT_BOOL, offcfg(confirm_exit)},

  // Command line
  {"Class", OPT_WSTRING, offcfg(class)},
  {"Hold", OPT_HOLD, offcfg(hold)},
  {"Daemonize", OPT_BOOL, offcfg(daemonize)},
  {"DaemonizeAlways", OPT_BOOL, offcfg(daemonize_always)},
  {"ExitWrite", OPT_BOOL, offcfg(exit_write)},
  {"ExitTitle", OPT_WSTRING, offcfg(exit_title)},
  {"Icon", OPT_WSTRING, offcfg(icon)},
  {"Log", OPT_WSTRING, offcfg(log)},
  {"Title", OPT_WSTRING, offcfg(title)},
  {"Utmp", OPT_BOOL, offcfg(utmp)},
  {"Window", OPT_WINDOW, offcfg(window)},
  {"X", OPT_INT, offcfg(x)},
  {"Y", OPT_INT, offcfg(y)},

  // "Hidden"
  {"AppID", OPT_WSTRING, offcfg(app_id)},
  {"AppName", OPT_WSTRING, offcfg(app_name)},
  {"AppLaunchCmd", OPT_WSTRING, offcfg(app_launch_cmd)},
  {"ColSpacing", OPT_INT, offcfg(col_spacing)},
  {"RowSpacing", OPT_INT, offcfg(row_spacing)},
  {"Padding", OPT_INT, offcfg(padding)},
  {"WordChars", OPT_STRING, offcfg(word_chars)},
  {"WordCharsExcl", OPT_STRING, offcfg(word_chars_excl)},
  {"IMECursorColour", OPT_COLOUR, offcfg(ime_cursor_colour)},

  // ANSI colours
  {"Black", OPT_COLOUR, offcfg(ansi_colours[BLACK_I])},
  {"Red", OPT_COLOUR, offcfg(ansi_colours[RED_I])},
  {"Green", OPT_COLOUR, offcfg(ansi_colours[GREEN_I])},
  {"Yellow", OPT_COLOUR, offcfg(ansi_colours[YELLOW_I])},
  {"Blue", OPT_COLOUR, offcfg(ansi_colours[BLUE_I])},
  {"Magenta", OPT_COLOUR, offcfg(ansi_colours[MAGENTA_I])},
  {"Cyan", OPT_COLOUR, offcfg(ansi_colours[CYAN_I])},
  {"White", OPT_COLOUR, offcfg(ansi_colours[WHITE_I])},
  {"BoldBlack", OPT_COLOUR, offcfg(ansi_colours[BOLD_BLACK_I])},
  {"BoldRed", OPT_COLOUR, offcfg(ansi_colours[BOLD_RED_I])},
  {"BoldGreen", OPT_COLOUR, offcfg(ansi_colours[BOLD_GREEN_I])},
  {"BoldYellow", OPT_COLOUR, offcfg(ansi_colours[BOLD_YELLOW_I])},
  {"BoldBlue", OPT_COLOUR, offcfg(ansi_colours[BOLD_BLUE_I])},
  {"BoldMagenta", OPT_COLOUR, offcfg(ansi_colours[BOLD_MAGENTA_I])},
  {"BoldCyan", OPT_COLOUR, offcfg(ansi_colours[BOLD_CYAN_I])},
  {"BoldWhite", OPT_COLOUR, offcfg(ansi_colours[BOLD_WHITE_I])},

  // Legacy
  {"BoldAsBright", OPT_BOOL | OPT_LEGACY, offcfg(bold_as_colour)},
  {"FontQuality", OPT_FONTSMOOTH | OPT_LEGACY, offcfg(font_smoothing)},
};

typedef const struct {
  string name;
  char val;
} opt_val;

static opt_val
*const opt_vals[] = {
  [OPT_BOOL] = (opt_val[]) {
    {"no", false},
    {"yes", true},
    {"false", false},
    {"true", true},
    {"off", false},
    {"on", true},
    {0, 0}
  },
  [OPT_MOD] = (opt_val[]) {
    {"off", 0},
    {"shift", MDK_SHIFT},
    {"alt", MDK_ALT},
    {"ctrl", MDK_CTRL},
    {0, 0}
  },
  [OPT_TRANS] = (opt_val[]) {
    {"off", TR_OFF},
    {"low", TR_LOW},
    {"medium", TR_MEDIUM},
    {"high", TR_HIGH},
    {"glass", TR_GLASS},
    {0, 0}
  },
  [OPT_CURSOR] = (opt_val[]) {
    {"line", CUR_LINE},
    {"block", CUR_BLOCK},
    {"underscore", CUR_UNDERSCORE},
    {0, 0}
  },
  [OPT_FONTSMOOTH] = (opt_val[]) {
    {"default", FS_DEFAULT},
    {"none", FS_NONE},
    {"partial", FS_PARTIAL},
    {"full", FS_FULL},
    {0, 0}
  },
  [OPT_MIDDLECLICK] = (opt_val[]) {
    {"enter", MC_ENTER},
    {"paste", MC_PASTE},
    {"extend", MC_EXTEND},
    {"void", MC_VOID},
    {0, 0}
  },
  [OPT_RIGHTCLICK] = (opt_val[]) {
    {"enter", RC_ENTER},
    {"paste", RC_PASTE},
    {"extend", RC_EXTEND},
    {"menu", RC_MENU},
    {0, 0}
  },
  [OPT_SCROLLBAR] = (opt_val[]) {
    {"left", -1},
    {"right", 1},
    {"none", 0},
    {0, 0}
  },
  [OPT_WINDOW] = (opt_val[]){
    {"hide", 0},   // SW_HIDE
    {"normal", 1}, // SW_SHOWNORMAL
    {"min", 2},    // SW_SHOWMINIMIZED
    {"max", 3},    // SW_SHOWMAXIMIZED
    {"full", -1},
    {0, 0}
  },
  [OPT_HOLD] = (opt_val[]) {
    {"never", HOLD_NEVER},
    {"start", HOLD_START},
    {"error", HOLD_ERROR},
    {"always", HOLD_ALWAYS},
    {0, 0}
  }
};

static int
find_option(string name)
{
  for (uint i = 0; i < lengthof(options); i++) {
    if (!strcasecmp(name, options[i].name))
      return i;
  }
  fprintf(stderr, "Ignoring unknown option '%s'.\n", name);
  return -1;
}

static uchar file_opts[lengthof(options)];
static uchar arg_opts[lengthof(options)];
static uint file_opts_num, arg_opts_num;

static bool
seen_file_option(uint i)
{
  return memchr(file_opts, i, file_opts_num);
}

static void
remember_file_option(uint i)
{
  if (i > 255) {
    fprintf(stderr, "Internal error: too many options.\n");
    exit(1);
  }

  if (!seen_file_option(i))
    file_opts[file_opts_num++] = i;
}

static bool
seen_arg_option(uint i)
{
  return memchr(arg_opts, i, arg_opts_num);
}

static void
remember_arg_option(uint i)
{
  if (i > 255) {
    fprintf(stderr, "Internal error: too many options.\n");
    exit(1);
  }

  if (!seen_arg_option(i))
    arg_opts[arg_opts_num++] = i;
}

void
remember_arg(string option)
{
  remember_arg_option(find_option(option));
}

static void
check_legacy_options(void (*remember_option)(uint))
{
  if (cfg.use_system_colours) {
    // Translate 'UseSystemColours' to colour settings.
    cfg.fg_colour = cfg.cursor_colour = win_get_sys_colour(true);
    cfg.bg_colour = win_get_sys_colour(false);
    cfg.use_system_colours = false;

    // Make sure they're written to the config file.
    // This assumes that the colour options are the first three in options[].
    remember_option(0);
    remember_option(1);
    remember_option(2);
  }
}

static struct {
  uchar r, g, b;
  char * name;
} xcolours[] = {
#include "rgb.t"
};

bool
parse_colour(string s, colour *cp)
{
  uint r, g, b;
  if (sscanf(s, "%u,%u,%u%c", &r, &g, &b, &(char){0}) == 3)
    ;
  else if (sscanf(s, "#%2x%2x%2x%c", &r, &g, &b, &(char){0}) == 3)
    ;
  else if (sscanf(s, "rgb:%2x/%2x/%2x%c", &r, &g, &b, &(char){0}) == 3)
    ;
  else if (sscanf(s, "rgb:%4x/%4x/%4x%c", &r, &g, &b, &(char){0}) == 3)
    r >>=8, g >>= 8, b >>= 8;
  else {
    int coli = -1;
    for (uint i = 0; i < lengthof(xcolours); i++)
      if (!strcmp(s, xcolours[i].name)) {
        r = xcolours[i].r;
        g = xcolours[i].g;
        b = xcolours[i].b;
        coli = i;
        break;
      }
    if (coli < 0)
      return false;
  }

  *cp = make_colour(r, g, b);
  return true;
}

static int
set_option(string name, string val_str, bool from_file)
{
  int i = find_option(name);
  if (i < 0)
    return i;

  void *val_p = (void *)&cfg + options[i].offset;
  uint type = options[i].type & ~OPT_LEGACY;

  switch (type) {
    when OPT_STRING:
      strset(val_p, val_str);
      return i;
    when OPT_WSTRING: {
      wchar * ws;
      if (from_file)
        ws = cs__utforansitowcs(val_str);
      else
        ws = cs__mbstowcs(val_str);
      wstrset(val_p, ws);
      free(ws);
      return i;
    }
    when OPT_INT: {
      char *val_end;
      int val = strtol(val_str, &val_end, 0);
      if (val_end != val_str) {
        *(int *)val_p = val;
        return i;
      }
    }
    when OPT_COLOUR:
      if (parse_colour(val_str, val_p))
        return i;
    otherwise: {
      int len = strlen(val_str);
      if (!len)
        break;
      for (opt_val *o = opt_vals[type]; o->name; o++) {
        if (!strncasecmp(val_str, o->name, len)) {
          *(char *)val_p = o->val;
          return i;
        }
      }
      // Value not found: try interpreting it as a number.
      char *val_end;
      int val = strtol(val_str, &val_end, 0);
      if (val_end != val_str) {
        *(char *)val_p = val;
        return i;
      }
    }
  }
  fprintf(stderr, "Ignoring invalid value '%s' for option '%s'.\n",
                  val_str, name);
  return -1;
}

static int
parse_option(string option, bool from_file)
{
  const char *eq = strchr(option, '=');
  if (!eq) {
    fprintf(stderr, "Ignoring malformed option '%s'.\n", option);
    return -1;
  }

  const char *name_end = eq;
  while (isspace((uchar)name_end[-1]))
    name_end--;

  uint name_len = name_end - option;
  char name[name_len + 1];
  memcpy(name, option, name_len);
  name[name_len] = 0;

  const char *val = eq + 1;
  while (isspace((uchar)*val))
    val++;

  return set_option(name, val, from_file);
}

static void
check_arg_option(int i)
{
  if (i >= 0) {
    remember_arg_option(i);
    check_legacy_options(remember_arg_option);
  }
}

void
set_arg_option(string name, string val)
{
  check_arg_option(set_option(name, val, false));
}

void
parse_arg_option(string option)
{
  check_arg_option(parse_option(option, false));
}

#ifdef debug_theme
#define trace_theme(params)	printf params
#else
#define trace_theme(params)
#endif

void
load_theme(wstring theme)
{
  wchar * theme_file = (wchar *)theme;
  bool free_theme_file = false;
  if (*theme && !wcschr(theme, L'/') && !wcschr(theme, L'\\')) {
    string subfolder = ".mintty/themes";
    char rcdir[strlen(home) + strlen(subfolder) + 2];
    sprintf(rcdir, "%s/%s", home, subfolder);
    wchar * rcpat = cygwin_create_path(CCP_POSIX_TO_WIN_W, rcdir);
    int len = wcslen(rcpat);
    rcpat = renewn(rcpat, len + wcslen(theme_file) + 2);
    rcpat[len++] = L'/';
    wcscpy(&rcpat[len], theme_file);
    theme_file = rcpat;
    free_theme_file = true;
  }
  char * filename = cygwin_create_path(CCP_WIN_W_TO_POSIX, theme_file);
  if (free_theme_file)
    free(theme_file);
  trace_theme(("load_theme -> <%s>\n", filename));
  load_config(filename, false);
  free(filename);
}

void
load_config(string filename, bool to_save)
{
#ifdef old_config
#else
  if (!to_save) {
    // restore base configuration, without theme mix-ins
    copy_config(&cfg, &file_cfg);
    trace_theme(("[loa] copy_config cfg<-file\n"));
  }
#endif

  if (access(filename, R_OK) == 0 && access(filename, W_OK) < 0)
    to_save = false;

#ifdef mis_config
  wchar * old_theme_file = wcsdup(cfg.theme_file);
#endif

  if (to_save) {
    file_opts_num = arg_opts_num = 0;

    delete(rc_filename);
#if CYGWIN_VERSION_API_MINOR >= 222
    rc_filename = cygwin_create_path(CCP_POSIX_TO_WIN_W, filename);
#else
    rc_filename = strdup(filename);
#endif
  }
#ifdef debug_config
  printf ("will save to %s? %d\n", filename, to_save);
#endif

  FILE *file = fopen(filename, "r");
  if (file) {
    char line[256];
    while (fgets(line, sizeof line, file)) {
      line[strcspn(line, "\r\n")] = 0;  /* trim newline */
      if (line[0] != '#' && line[0] != '\0') {
        int i = parse_option(line, true);
        if (to_save && i >= 0)
          remember_file_option(i);
      }
    }
    fclose(file);
  }

  check_legacy_options(remember_file_option);

#ifdef old_config
  copy_config(&file_cfg, &cfg);
#else
  if (to_save) {
    copy_config(&file_cfg, &cfg);
    trace_theme(("[sav] copy_config file<-cfg\n"));
  }

#ifdef mis_config
  bool theme_changed = wcscmp(old_theme_file, cfg.theme_file);
  free(old_theme_file);

  if (to_save && theme_changed)
    load_theme(cfg.theme_file);
#endif
#endif
}

void
copy_config(config *dst_p, const config *src_p)
{
  for (uint i = 0; i < lengthof(options); i++) {
    opt_type type = options[i].type;
    if (!(type & OPT_LEGACY)) {
      uint offset = options[i].offset;
      void *dst_val_p = (void *)dst_p + offset;
      void *src_val_p = (void *)src_p + offset;
      switch (type) {
        when OPT_STRING:
          strset(dst_val_p, *(string *)src_val_p);
        when OPT_WSTRING:
          wstrset(dst_val_p, *(wstring *)src_val_p);
        when OPT_INT or OPT_COLOUR:
          *(int *)dst_val_p = *(int *)src_val_p;
        otherwise:
          *(char *)dst_val_p = *(char *)src_val_p;
      }
    }
  }
}

void
init_config(void)
{
  copy_config(&cfg, &default_cfg);
  trace_theme(("[ini] copy_config cfg<-default\n"));
}

void
finish_config(void)
{
  // Avoid negative sizes.
  cfg.rows = max(1, cfg.rows);
  cfg.cols = max(1, cfg.cols);
  cfg.scrollback_lines = max(0, cfg.scrollback_lines);

  // Ignore charset setting if we haven't got a locale.
  if (!*cfg.locale)
    strset(&cfg.charset, "");

  // bold_as_font used to be implied by !bold_as_colour.
  if (cfg.bold_as_font == -1) {
    cfg.bold_as_font = !cfg.bold_as_colour;
    remember_file_option(find_option("BoldAsFont"));
  }

  if (0 < cfg.transparency && cfg.transparency <= 3)
    cfg.transparency *= 16;
}

static void
save_config(void)
{
  string filename;

#if CYGWIN_VERSION_API_MINOR >= 222
  filename = cygwin_create_path(CCP_WIN_W_TO_POSIX, rc_filename);
#else
  filename = rc_filename;
#endif

  FILE *file = fopen(filename, "w");

  if (!file) {
    char *msg;
    int len = asprintf(&msg, "Could not save options to '%s':\n%s.",
                       filename, strerror(errno));
    if (len > 0) {
      wchar wmsg[len + 1];
      if (cs_mbstowcs(wmsg, msg, lengthof(wmsg)) >= 0)
        win_show_error(wmsg);
      delete(msg);
    }
  }
  else {
    for (uint j = 0; j < file_opts_num; j++) {
      uint i = file_opts[j];
      opt_type type = options[i].type;
      if (!(type & OPT_LEGACY)) {
        fprintf(file, "%s=", options[i].name);
#ifdef old_config
        void *cfg_p = seen_arg_option(i) ? &file_cfg : &cfg;
#else
        void *cfg_p = &file_cfg;
#endif
        void *val_p = cfg_p + options[i].offset;
        switch (type) {
          when OPT_STRING:
            fprintf(file, "%s", *(string *)val_p);
          when OPT_WSTRING: {
            char * s = cs__wcstoutf(*(wstring *)val_p);
            fprintf(file, "%s", s);
            free(s);
          }
          when OPT_INT:
            fprintf(file, "%i", *(int *)val_p);
          when OPT_COLOUR: {
            colour c = *(colour *)val_p;
            fprintf(file, "%u,%u,%u", red(c), green(c), blue(c));
          }
          otherwise: {
            int val = *(char *)val_p;
            opt_val *o = opt_vals[type];
            for (; o->name; o++) {
              if (o->val == val)
                break;
            }
            if (o->name)
              fputs(o->name, file);
            else
              fprintf(file, "%i", val);
          }
        }
        fputc('\n', file);
      }
    }
    fclose(file);
  }

#if CYGWIN_VERSION_API_MINOR >= 222
  delete(filename);
#endif
}


static control *cols_box, *rows_box, *locale_box, *charset_box;

void
apply_config(bool save)
{
  // Record what's changed
  for (uint i = 0; i < lengthof(options); i++) {
    opt_type type = options[i].type;
    uint offset = options[i].offset;
#ifdef old_config
    void *val_p = (void *)&cfg + offset;
#else
    void *val_p = (void *)&file_cfg + offset;
#endif
    void *new_val_p = (void *)&new_cfg + offset;
    bool changed;
    switch (type) {
      when OPT_STRING:
        changed = strcmp(*(string *)val_p, *(string *)new_val_p);
      when OPT_WSTRING:
        changed = wcscmp(*(wstring *)val_p, *(wstring *)new_val_p);
      when OPT_INT or OPT_COLOUR:
        changed = (*(int *)val_p != *(int *)new_val_p);
      otherwise:
        changed = (*(char *)val_p != *(char *)new_val_p);
    }
    if (changed && !seen_arg_option(i))
      remember_file_option(i);
  }

#ifdef old_config
  win_reconfig();  // copy_config(&cfg, &new_cfg);
  if (save)
    save_config();
#else
  copy_config(&file_cfg, &new_cfg);
  trace_theme(("[app] copy_config file<-new\n"));
  if (save)
    save_config();
  bool had_theme = !!*cfg.theme_file;

  win_reconfig();  // copy_config(&cfg, &new_cfg);
  trace_theme(("[rec] copy_config cfg<-new\n"));
  trace_theme(("green %06X new %06X file %06X bg %06X new %06X file %06X\n", cfg.ansi_colours[GREEN_I], new_cfg.ansi_colours[GREEN_I], file_cfg.ansi_colours[GREEN_I], cfg.bg_colour, new_cfg.bg_colour, file_cfg.bg_colour));

  if (*cfg.theme_file) {
    load_theme(cfg.theme_file);
    trace_theme(("green %06X new %06X file %06X bg %06X new %06X file %06X\n", cfg.ansi_colours[GREEN_I], new_cfg.ansi_colours[GREEN_I], file_cfg.ansi_colours[GREEN_I], cfg.bg_colour, new_cfg.bg_colour, file_cfg.bg_colour));
    win_reset_colours();
  }
  else if (had_theme)
    win_reset_colours();
#endif
}

static void
ok_handler(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION) {
    apply_config(true);
    dlg_end();
  }
}

static void
cancel_handler(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION)
    dlg_end();
}

static void
apply_handler(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION)
    apply_config(false);
}

static void
about_handler(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION)
    win_show_about();
}

static void
current_size_handler(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION) {
    new_cfg.cols = term.cols;
    new_cfg.rows = term.rows;
    dlg_refresh(cols_box);
    dlg_refresh(rows_box);
  }
}

static void
printer_handler(control *ctrl, int event)
{
  const wstring NONE = L"◇ None (printing disabled) ◇";  // ♢◇
  const wstring CFG_NONE = L"";
  const wstring DEFAULT = L"◆ Default printer ◆";  // ♦◆
  const wstring CFG_DEFAULT = L"*";
  wstring printer = new_cfg.printer;
  if (event == EVENT_REFRESH) {
    dlg_listbox_clear(ctrl);
    dlg_listbox_add_w(ctrl, NONE);
    dlg_listbox_add_w(ctrl, DEFAULT);
    uint num = printer_start_enum();
    for (uint i = 0; i < num; i++)
      dlg_listbox_add_w(ctrl, (wchar *)printer_get_name(i));
    printer_finish_enum();
    if (*printer == '*')
      dlg_editbox_set_w(ctrl, DEFAULT);
    else
      dlg_editbox_set_w(ctrl, *printer ? printer : NONE);
  }
  else if (event == EVENT_VALCHANGE || event == EVENT_SELCHANGE) {
    dlg_editbox_get_w(ctrl, &printer);
    if (!wcscmp(printer, NONE))
      wstrset(&printer, CFG_NONE);
    else if (!wcscmp(printer, DEFAULT))
      wstrset(&printer, CFG_DEFAULT);
    new_cfg.printer = printer;
  }
}

static void
set_charset(string charset)
{
  strset(&new_cfg.charset, charset);
  dlg_editbox_set(charset_box, charset);
}

static void
locale_handler(control *ctrl, int event)
{
  string locale = new_cfg.locale;
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      string l;
      for (int i = 0; (l = locale_menu[i]); i++)
        dlg_listbox_add(ctrl, l);
      dlg_editbox_set(ctrl, locale);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, locale);
      if (!*locale)
        set_charset("");
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.locale);
    when EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.locale);
      if (*locale == '(')
        strset(&locale, "");
      if (!*locale)
        set_charset("");
#if HAS_LOCALES
      else if (!*new_cfg.charset)
        set_charset("UTF-8");
#endif
      new_cfg.locale = locale;
  }
}

static void
check_locale(void)
{
  if (!*new_cfg.locale) {
    strset(&new_cfg.locale, "C");
    dlg_editbox_set(locale_box, "C");
  }
}

static void
charset_handler(control *ctrl, int event)
{
  string charset = new_cfg.charset;
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      string cs;
      for (int i = 0; (cs = charset_menu[i]); i++)
        dlg_listbox_add(ctrl, cs);
      dlg_editbox_set(ctrl, charset);
    when EVENT_UNFOCUS:
      dlg_editbox_set(ctrl, charset);
      if (*charset)
        check_locale();
    when EVENT_VALCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.charset);
    when EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &charset);
      if (*charset == '(')
        strset(&charset, "");
      else {
        *strchr(charset, ' ') = 0;
        check_locale();
      }
      new_cfg.charset = charset;
  }
}

static void
term_handler(control *ctrl, int event)
{
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      dlg_listbox_add(ctrl, "xterm");
      dlg_listbox_add(ctrl, "xterm-256color");
      dlg_listbox_add(ctrl, "xterm-vt220");
      dlg_listbox_add(ctrl, "vt100");
      dlg_listbox_add(ctrl, "vt220");
      dlg_editbox_set(ctrl, new_cfg.term);
    when EVENT_VALCHANGE or EVENT_SELCHANGE:
      dlg_editbox_get(ctrl, &new_cfg.term);
  }
}

//  1 -> 0x00000000 MB_OK              Default Beep
//  2 -> 0x00000010 MB_ICONSTOP        Critical Stop
//  3 -> 0x00000020 MB_ICONQUESTION    Question
//  4 -> 0x00000030 MB_ICONEXCLAMATION Exclamation
//  5 -> 0x00000040 MB_ICONASTERISK    Asterisk
// -1 -> 0xFFFFFFFF                    Simple Beep
static string beeps[] = {
  "simple",
  "none",
  "default",
  "Critical Stop",
  "Question",
  "Exclamation",
  "Asterisk",
};

static void
bell_handler(control *ctrl, int event)
{
  switch (event) {
    when EVENT_REFRESH:
      dlg_listbox_clear(ctrl);
      int sel = -1;
      for (uint i = 0; i < lengthof(beeps); i++) {
        dlg_listbox_add(ctrl, beeps[i]);
        if ((int)i == new_cfg.bell_type + 1)
          sel = i;
      }
      if (sel >= 0)
        dlg_editbox_set(ctrl, beeps[sel]);
    when EVENT_VALCHANGE or EVENT_SELCHANGE: {
      char * beep = malloc(0);
      dlg_editbox_get(ctrl, (string *)&beep);
      for (uint i = 0; i < lengthof(beeps); i++) {
        if (!strcmp(beep, beeps[i])) {
          new_cfg.bell_type = i - 1;
          break;
        }
      }
      free(beep);
      win_bell(&new_cfg);
    }
  }
}

#include "winpriv.h"  // home

static void
add_file_resources(control *ctrl, wstring pattern)
{
  string subfolder = ".mintty";
  char rcdir[strlen(home) + strlen(subfolder) + 2];
  sprintf(rcdir, "%s/%s", home, subfolder);
  wchar * rcpat = cygwin_create_path(CCP_POSIX_TO_WIN_W, rcdir);
  int len = wcslen(rcpat);
  rcpat = renewn(rcpat, len + wcslen(pattern) + 2);
  rcpat[len++] = L'/';
  wcscpy(&rcpat[len], pattern);

  wstring suf = wcsrchr(pattern, L'.');
  int sufl = suf ? wcslen(suf) : 0;

  WIN32_FIND_DATAW ffd;
  HANDLE hFind = FindFirstFileW(rcpat, &ffd);
  int ok = hFind != INVALID_HANDLE_VALUE;
  //if (!ok) retry with "/usr/share/mintty"?
  //(then check also win_bell() and load_theme())
  while (ok) {
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // skip
    }
    else {
      //LARGE_INTEGER filesize = {.LowPart = ffd.nFileSizeLow, .HighPart = ffd.nFileSizeHigh};
      //long s = filesize.QuadPart;

      // strip suffix
      int len = wcslen(ffd.cFileName);
      if (ffd.cFileName[0] != '.' && ffd.cFileName[len - 1] != '~') {
        ffd.cFileName[len - sufl] = 0;
        dlg_listbox_add_w(ctrl, ffd.cFileName);
      }
    }
    ok = FindNextFileW(hFind, &ffd);
  }
  FindClose(hFind);
}

static void
bellfile_handler(control *ctrl, int event)
{
  const wstring NONE = L"◇ None (system sound) ◇";  // ♢◇
  const wstring CFG_NONE = L"";
  wstring bell_file = new_cfg.bell_file;
  if (event == EVENT_REFRESH) {
    dlg_listbox_clear(ctrl);
    dlg_listbox_add_w(ctrl, NONE);
    add_file_resources(ctrl, L"sounds/*.wav"); //  dlg_listbox_add_w(ctrl, L"...");
    // strip std dir prefix...
    dlg_editbox_set_w(ctrl, *bell_file ? bell_file : NONE);
  }
  else if (event == EVENT_VALCHANGE || event == EVENT_SELCHANGE) {
    dlg_editbox_get_w(ctrl, &bell_file);
    if (!wcscmp(bell_file, NONE))
      wstrset(&bell_file, CFG_NONE);
    // add std dir prefix?
    new_cfg.bell_file = bell_file;
    win_bell(&new_cfg);
  }
}

static void
theme_handler(control *ctrl, int event)
{
  const wstring NONE = L"◇ None ◇";  // ♢◇
  const wstring CFG_NONE = L"";
  wstring theme_file = new_cfg.theme_file;
  if (event == EVENT_REFRESH) {
    dlg_listbox_clear(ctrl);
    dlg_listbox_add_w(ctrl, NONE);
    add_file_resources(ctrl, L"themes/*"); //  dlg_listbox_add_w(ctrl, L"...");
    dlg_editbox_set_w(ctrl, *theme_file ? theme_file : NONE);
  }
  else if (event == EVENT_VALCHANGE || event == EVENT_SELCHANGE) {
    dlg_editbox_get_w(ctrl, &theme_file);
    if (!wcscmp(theme_file, NONE))
      wstrset(&theme_file, CFG_NONE);
    new_cfg.theme_file = theme_file;
  }
}

static void
bell_tester(control *unused(ctrl), int event)
{
  if (event == EVENT_ACTION)
    win_bell(&new_cfg);
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
  s = ctrl_new_set(b, "", "");
  ctrl_columns(s, 5, 20, 20, 20, 20, 20);
  c = ctrl_pushbutton(s, "About...", about_handler, 0);
  c->column = 0;
  c = ctrl_pushbutton(s, "Save", ok_handler, 0);
  c->button.isdefault = true;
  c->column = 2;
  c = ctrl_pushbutton(s, "Cancel", cancel_handler, 0);
  c->button.iscancel = true;
  c->column = 3;
  c = ctrl_pushbutton(s, "Apply", apply_handler, 0);
  c->column = 4;

 /*
  * The Looks panel.
  */
  s = ctrl_new_set(b, "Looks", "Colours");
  ctrl_columns(s, 3, 33, 33, 33);
  ctrl_pushbutton(
    s, "&Foreground...", dlg_stdcolour_handler, &new_cfg.fg_colour
  )->column = 0;
  ctrl_pushbutton(
    s, "&Background...", dlg_stdcolour_handler, &new_cfg.bg_colour
  )->column = 1;
  ctrl_pushbutton(
    s, "&Cursor...", dlg_stdcolour_handler, &new_cfg.cursor_colour
  )->column = 2;
  ctrl_combobox(
    s, "&Theme", 80, theme_handler, &new_cfg.theme_file
  );

  s = ctrl_new_set(b, "Looks", "Transparency");
  bool with_glass = win_is_glass_available();
  ctrl_radiobuttons(
    s, null, 4 + with_glass,
    dlg_stdradiobutton_handler, &new_cfg.transparency,
    "&Off", TR_OFF,
    "&Low", TR_LOW,
    with_glass ? "&Med." : "&Medium", TR_MEDIUM,
    "&High", TR_HIGH,
    with_glass ? "Gla&ss" : null, TR_GLASS,
    null
  );
#ifdef support_blurred
  ctrl_columns(s, 2, with_glass ? 80 : 75, with_glass ? 20 : 25);
  ctrl_checkbox(
    s, "Opa&que when focused",
    dlg_stdcheckbox_handler, &new_cfg.opaque_when_focused
  )->column = 0;
  ctrl_checkbox(
    s, "&Blur",
    dlg_stdcheckbox_handler, &new_cfg.blurred
  )->column = 1;
#else
  ctrl_checkbox(
    s, "Opa&que when focused",
    dlg_stdcheckbox_handler, &new_cfg.opaque_when_focused
  );
#endif

  s = ctrl_new_set(b, "Looks", "Cursor");
  ctrl_radiobuttons(
    s, null, 4 + with_glass,
    dlg_stdradiobutton_handler, &new_cfg.cursor_type,
    "Li&ne", CUR_LINE,
    "Bloc&k", CUR_BLOCK,
    "&Underscore", CUR_UNDERSCORE,
    null
  );
  ctrl_checkbox(
    s, "Blinkin&g", dlg_stdcheckbox_handler, &new_cfg.cursor_blinks
  );

 /*
  * The Text panel.
  */
  s = ctrl_new_set(b, "Text", "Font");
  ctrl_fontsel(
    s, null, dlg_stdfontsel_handler, &new_cfg.font
  );

  s = ctrl_new_set(b, "Text", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_radiobuttons(
    s, "Font smoothing", 2,
    dlg_stdradiobutton_handler, &new_cfg.font_smoothing,
    "&Default", FS_DEFAULT,
    "&None", FS_NONE,
    "&Partial", FS_PARTIAL,
    "&Full", FS_FULL,
    null
  )->column = 1;

  ctrl_checkbox(
    s, "Sho&w bold as font",
    dlg_stdcheckbox_handler, &new_cfg.bold_as_font
  )->column = 0;
  ctrl_checkbox(
    s, "Show &bold as colour",
    dlg_stdcheckbox_handler, &new_cfg.bold_as_colour
  )->column = 0;
  ctrl_checkbox(
    s, "&Allow blinking",
    dlg_stdcheckbox_handler, &new_cfg.allow_blinking
  )->column = 0;

  s = ctrl_new_set(b, "Text", null);
  ctrl_columns(s, 2, 29, 71);
  (locale_box = ctrl_combobox(
    s, "&Locale", 100, locale_handler, 0
  ))->column = 0;
  (charset_box = ctrl_combobox(
    s, "&Character set", 100, charset_handler, 0
  ))->column = 1;

 /*
  * The Keys panel.
  */
  s = ctrl_new_set(b, "Keys", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "&Backarrow sends ^H",
    dlg_stdcheckbox_handler, &new_cfg.backspace_sends_bs
  )->column = 0;
  ctrl_checkbox(
    s, "&Delete sends DEL",
    dlg_stdcheckbox_handler, &new_cfg.delete_sends_del
  )->column = 1;
  ctrl_checkbox(
    s, "Ctrl+LeftAlt is Alt&Gr",
    dlg_stdcheckbox_handler, &new_cfg.ctrl_alt_is_altgr
  );

  s = ctrl_new_set(b, "Keys", "Shortcuts");
  ctrl_checkbox(
    s, "Cop&y and Paste (Ctrl/Shift+Ins)",
    dlg_stdcheckbox_handler, &new_cfg.clip_shortcuts
  );
  ctrl_checkbox(
    s, "&Menu and Full Screen (Alt+Space/Enter)",
    dlg_stdcheckbox_handler, &new_cfg.window_shortcuts
  );
  ctrl_checkbox(
    s, "&Switch window (Ctrl+[Shift+]Tab)",
    dlg_stdcheckbox_handler, &new_cfg.switch_shortcuts
  );
  ctrl_checkbox(
    s, "&Zoom (Ctrl+plus/minus/zero)",
    dlg_stdcheckbox_handler, &new_cfg.zoom_shortcuts
  );
  ctrl_checkbox(
    s, "&Alt+Fn shortcuts",
    dlg_stdcheckbox_handler, &new_cfg.alt_fn_shortcuts
  );
  ctrl_checkbox(
    s, "&Ctrl+Shift+letter shortcuts",
    dlg_stdcheckbox_handler, &new_cfg.ctrl_shift_shortcuts
  );

 /*
  * The Mouse panel.
  */
  s = ctrl_new_set(b, "Mouse", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_checkbox(
    s, "Cop&y on select",
    dlg_stdcheckbox_handler, &new_cfg.copy_on_select
  )->column = 0;
  ctrl_checkbox(
    s, "Copy as &rich text",
    dlg_stdcheckbox_handler, &new_cfg.copy_as_rtf
  )->column = 1;
  ctrl_checkbox(
    s, "Clic&ks place command line cursor",
    dlg_stdcheckbox_handler, &new_cfg.clicks_place_cursor
  );

  s = ctrl_new_set(b, "Mouse", "Click actions");
  ctrl_radiobuttons(
    s, "Right mouse button", 4,
    dlg_stdradiobutton_handler, &new_cfg.right_click_action,
    "&Paste", RC_PASTE,
    "E&xtend", RC_EXTEND,
    "&Menu", RC_MENU,
    "Ente&r", RC_ENTER,
    null
  );
  ctrl_radiobuttons(
    s, "Middle mouse button", 4,
    dlg_stdradiobutton_handler, &new_cfg.middle_click_action,
    "&Paste", MC_PASTE,
    "E&xtend", MC_EXTEND,
    "&Nothing", MC_VOID,
    "Ente&r", MC_ENTER,
    null
  );

  s = ctrl_new_set(b, "Mouse", "Application mouse mode");
  ctrl_radiobuttons(
    s, "Default click target", 4,
    dlg_stdradiobutton_handler, &new_cfg.clicks_target_app,
    "&Window", false,
    "Applicatio&n", true,
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for overriding default", 4,
    dlg_stdradiobutton_handler, &new_cfg.click_target_mod,
    "&Shift", MDK_SHIFT,
    "&Ctrl", MDK_CTRL,
    "&Alt", MDK_ALT,
    "&Off", 0,
    null
  );

 /*
  * The Window panel.
  */
  s = ctrl_new_set(b, "Window", "Default size");
  ctrl_columns(s, 5, 35, 3, 28, 4, 30);
  (cols_box = ctrl_editbox(
    s, "Colu&mns", 44, dlg_stdintbox_handler, &new_cfg.cols
  ))->column = 0;
  (rows_box = ctrl_editbox(
    s, "Ro&ws", 55, dlg_stdintbox_handler, &new_cfg.rows
  ))->column = 2;
  ctrl_pushbutton(
    s, "C&urrent size", current_size_handler, 0
  )->column = 4;

  s = ctrl_new_set(b, "Window", null);
  ctrl_columns(s, 2, 66, 34);
  ctrl_editbox(
    s, "Scroll&back lines", 50,
    dlg_stdintbox_handler, &new_cfg.scrollback_lines
  )->column = 0;
  ctrl_radiobuttons(
    s, "Scrollbar", 4,
    dlg_stdradiobutton_handler, &new_cfg.scrollbar,
    "&Left", -1,
    "&None", 0,
    "&Right", 1,
    null
  );
  ctrl_radiobuttons(
    s, "Modifier for scrolling", 4,
    dlg_stdradiobutton_handler, &new_cfg.scroll_mod,
    "&Shift", MDK_SHIFT,
    "&Ctrl", MDK_CTRL,
    "&Alt", MDK_ALT,
    "&Off", 0,
    null
  );
  ctrl_checkbox(
    s, "&PgUp and PgDn scroll without modifier",
    dlg_stdcheckbox_handler, &new_cfg.pgupdn_scroll
  );

 /*
  * The Terminal panel.
  */
  s = ctrl_new_set(b, "Terminal", null);
  ctrl_columns(s, 2, 50, 50);
  ctrl_combobox(
    s, "&Type", 100, term_handler, 0
  )->column = 0;
  ctrl_editbox(
    s, "&Answerback", 100, dlg_stdstringbox_handler, &new_cfg.answerback
  )->column = 1;

  s = ctrl_new_set(b, "Terminal", "Bell (sound overridden by Wave/BellFile or BellFreq)");
  ctrl_columns(s, 3, 36, 19, 45);
  ctrl_combobox(
    s, null, 100, bell_handler, 0
  )->column = 0;
  ctrl_checkbox(
    s, "&Flash", dlg_stdcheckbox_handler, &new_cfg.bell_flash
  )->column = 1;
  ctrl_checkbox(
    s, "&Highlight in taskbar", dlg_stdcheckbox_handler, &new_cfg.bell_taskbar
  )->column = 2;
  ctrl_columns(s, 1, 100);  // reset column stuff so we can rearrange them
  ctrl_columns(s, 2, 82, 18);
#ifdef use_belleditbox
  ctrl_editbox(
    s, "&Wave", 83, dlg_stdstringbox_handler, &new_cfg.bell_file
  )->column = 0;
#else
  ctrl_combobox(
    s, "&Wave", 83, bellfile_handler, &new_cfg.bell_file
  )->column = 0;
#endif
  ctrl_pushbutton(
    s, "&Play", bell_tester, 0
  )->column = 1;

  s = ctrl_new_set(b, "Terminal", "Printer");
#ifdef use_multi_listbox_for_printers
  ctrl_listbox(
    s, null, 4, 100, printer_handler, 0
  );
#else
  ctrl_combobox(
    s, null, 100, printer_handler, 0
  );
#endif

  s = ctrl_new_set(b, "Terminal", null);
  ctrl_checkbox(
    s, "&Prompt about running processes on close",
    dlg_stdcheckbox_handler, &new_cfg.confirm_exit
  );
}
