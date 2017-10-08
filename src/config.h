#ifndef CONFIG_H
#define CONFIG_H


// Enums for various options.

typedef enum { MDK_SHIFT = 1, MDK_ALT = 2, MDK_CTRL = 4 } mod_keys;
enum { HOLD_NEVER, HOLD_START, HOLD_ERROR, HOLD_ALWAYS };
enum { CUR_BLOCK, CUR_UNDERSCORE, CUR_LINE };
enum { FS_DEFAULT, FS_PARTIAL, FS_NONE, FS_FULL };
enum { FR_TEXTOUT, FR_UNISCRIBE };
enum { MC_VOID, MC_PASTE, MC_EXTEND, MC_ENTER };
enum { RC_MENU, RC_PASTE, RC_EXTEND, RC_ENTER };
enum { TR_OFF = 0, TR_LOW = 16, TR_MEDIUM = 32, TR_HIGH = 48, TR_GLASS = -1 };
enum { FLASH_FRAME = 1, FLASH_BORDER = 2, FLASH_FULL = 4, FLASH_REVERSE = 8 };


// Colour values.

typedef uint colour;

enum { DEFAULT_COLOUR = UINT_MAX };

static inline colour
make_colour(uchar r, uchar g, uchar b) { return r | g << 8 | b << 16; }

extern bool parse_colour(string, colour *);

static inline uchar red(colour c) { return c; }
static inline uchar green(colour c) { return c >> 8; }
static inline uchar blue(colour c) { return c >> 16; }


// Font properties.

typedef struct {
  wstring name;
  int size;
  int weight;
  bool isbold;
} font_spec;


// Configuration data.

typedef struct {
  // Looks
  colour fg_colour, bold_colour, bg_colour, cursor_colour, underl_colour;
  bool underl_manual;
  colour sel_fg_colour, sel_bg_colour;
  colour search_fg_colour, search_bg_colour, search_current_colour;
  wstring theme_file;
  string colour_scheme;
  char transparency;
  bool blurred;
  bool opaque_when_focused;
  char cursor_type;
  bool cursor_blinks;
  // Text
  font_spec font;
  font_spec fontfams[11];
  wstring font_sample;
  bool show_hidden_fonts;
  char font_smoothing;
  char font_render;
  char bold_as_font;    // 0 = false, 1 = true, -1 = undefined
  bool bold_as_colour;
  bool allow_blinking;
  string locale;
  string charset;
  int fontmenu;
  // Keys
  bool backspace_sends_bs;
  bool delete_sends_del;
  bool ctrl_alt_is_altgr;
  bool old_altgr_detection;
  bool clip_shortcuts;
  bool window_shortcuts;
  bool switch_shortcuts;
  bool zoom_shortcuts;
  bool zoom_font_with_window;
  bool alt_fn_shortcuts;
  bool ctrl_shift_shortcuts;
  bool ctrl_exchange_shift;
  bool ctrl_controls;
  char compose_key;
  string key_prtscreen;	// VK_SNAPSHOT
  string key_pause;	// VK_PAUSE
  string key_break;	// VK_CANCEL
  string key_menu;	// VK_APPS
  string key_scrlock;	// VK_SCROLL
  // Mouse
  bool copy_on_select;
  bool copy_as_rtf;
  bool clicks_place_cursor;
  char middle_click_action;
  char right_click_action;
  int opening_clicks;
  bool zoom_mouse;
  bool clicks_target_app;
  char click_target_mod;
  bool hide_mouse;
  // Window
  int cols, rows;
  int scrollback_lines;
  char scrollbar;
  char scroll_mod;
  bool pgupdn_scroll;
  wstring lang;
  string search_bar;
  // Terminal
  string term;
  wstring answerback;
  bool old_wrapmodes;
  bool bell_sound;
  int bell_type;
  wstring bell_file;
  int bell_freq;
  int bell_len;
  bool bell_flash;   // xterm: visualBell
  int bell_flash_style;
  bool bell_taskbar; // xterm: bellIsUrgent
  bool bell_popup;   // xterm: popOnBell
  wstring printer;
  bool confirm_exit;
  bool allow_set_selection;
  // Command line
  wstring class;
  char hold;
  bool exit_write;
  wstring exit_title;
  wstring icon;
  wstring log;
  bool logging;
  wstring title;
  bool create_utmp;
  char window;
  int x, y;
  bool daemonize;
  bool daemonize_always;
  // "Hidden"
  int bidi;
  bool disable_alternate_screen;
  wstring app_id;
  wstring app_name;
  wstring app_launch_cmd;
  wstring drop_commands;
  wstring user_commands;
  int col_spacing, row_spacing;
  int padding;
  bool handle_dpichanged;
  int check_version_update;
  string word_chars;
  string word_chars_excl;
  colour ime_cursor_colour;
  colour ansi_colours[16];
  wstring sixel_clip_char;
  // Legacy
  bool use_system_colours;
} config;

extern string config_dir;
extern config cfg, new_cfg, file_cfg;

extern void init_config(void);
extern void list_fonts(bool report);
extern void load_config(string filename, int to_save);
extern void load_theme(wstring theme);
extern char * get_resource_file(wstring sub, wstring res, bool towrite);
extern void load_scheme(string colour_scheme);
extern void set_arg_option(string name, string val);
extern void parse_arg_option(string);
extern void remember_arg(string);
extern void finish_config(void);
extern void copy_config(char * tag, config * dst, const config * src);
extern void apply_config(bool save);

#endif
