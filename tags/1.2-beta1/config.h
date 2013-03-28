#ifndef CONFIG_H
#define CONFIG_H


// Enums for various options.

typedef enum { MDK_SHIFT = 1, MDK_ALT = 2, MDK_CTRL = 4 } mod_keys;
enum { HOLD_NEVER, HOLD_START, HOLD_ERROR, HOLD_ALWAYS };
enum { CUR_BLOCK, CUR_UNDERSCORE, CUR_LINE };
enum { FS_DEFAULT, FS_PARTIAL, FS_NONE, FS_FULL };
enum { RC_MENU, RC_PASTE, RC_EXTEND };
enum { TR_OFF = 0, TR_LOW = 16, TR_MEDIUM = 32, TR_HIGH = 48, TR_GLASS = -1 };


// Colour values.

typedef uint colour;

enum { DEFAULT_COLOUR = UINT_MAX };

static inline colour
make_colour(uchar r, uchar g, uchar b) { return r | g << 8 | b << 16; }

bool parse_colour(string, colour *);

static inline uchar red(colour c) { return c; }
static inline uchar green(colour c) { return c >> 8; }
static inline uchar blue(colour c) { return c >> 16; }


// Font properties.

typedef struct {
  string name;
  int size;
  bool isbold;
} font_spec;


// Configuration data.

typedef struct {
  // Looks
  colour fg_colour, bg_colour, cursor_colour;
  char transparency;
  bool opaque_when_focused;
  char cursor_type;
  bool cursor_blinks;
  // Text
  font_spec font;
  char font_smoothing;
  char bold_as_font;    // 0 = false, 1 = true, -1 = undefined
  bool bold_as_colour;
  bool allow_blinking;
  string locale;
  string charset;
  // Keys
  bool backspace_sends_bs;
  bool ctrl_alt_is_altgr;
  bool clip_shortcuts;
  bool window_shortcuts;
  bool switch_shortcuts;
  bool zoom_shortcuts;
  bool alt_fn_shortcuts;
  bool ctrl_shift_shortcuts;
  // Mouse
  bool copy_on_select;
  bool copy_as_rtf;
  bool clicks_place_cursor;
  char right_click_action;
  bool clicks_target_app;
  char click_target_mod;
  // Window
  int cols, rows;
  int scrollback_lines;
  char scrollbar;
  char scroll_mod;
  bool pgupdn_scroll;
  // Terminal
  string term;
  string answerback;
  bool bell_sound;
  bool bell_flash;
  bool bell_taskbar;
  string printer;
  bool confirm_exit;
  // Command line
  string class;
  char hold;
  string icon;
  string log;
  string title;
  bool utmp;
  char window;
  int x, y;
  // "Hidden"
  string app_id;
  int col_spacing, row_spacing;
  string word_chars;
  colour ime_cursor_colour;
  colour ansi_colours[16];
  // Legacy
  bool use_system_colours;
} config;

extern config cfg, new_cfg;

void init_config(void);
void load_config(string filename);
void set_arg_option(string name, string val);
void parse_arg_option(string);
void remember_arg(string);
void finish_config(void);
void copy_config(config *dst, const config *src);

#endif
