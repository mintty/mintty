#ifndef CONFIG_H
#define CONFIG_H


// Enums for various options.

typedef enum { MDK_SHIFT = 1, MDK_ALT = 2, MDK_CTRL = 4, 
               MDK_WIN = 8, MDK_SUPER = 16, MDK_HYPER = 32 } mod_keys;
enum { HOLD_NEVER, HOLD_START, HOLD_ERROR, HOLD_ALWAYS };
enum { CUR_BLOCK, CUR_UNDERSCORE, CUR_LINE, CUR_BOX };
enum { FS_DEFAULT, FS_PARTIAL, FS_NONE, FS_FULL };
enum { FR_TEXTOUT, FR_UNISCRIBE };
enum { MC_VOID, MC_PASTE, MC_EXTEND, MC_ENTER };
enum { RC_MENU, RC_PASTE, RC_EXTEND, RC_ENTER };
enum { TR_OFF = 0, TR_LOW = 16, TR_MEDIUM = 32, TR_HIGH = 48, TR_GLASS = -1 };
enum { FLASH_FRAME = 1, FLASH_BORDER = 2, FLASH_FULL = 4, FLASH_REVERSE = 8 };
enum { EMOJIS_NONE = 0, EMOJIS_ONE = 1, EMOJIS_NOTO = 2, EMOJIS_APPLE = 3, 
       EMOJIS_GOOGLE = 4, EMOJIS_TWITTER = 5, EMOJIS_FB = 6, 
       EMOJIS_SAMSUNG = 7, EMOJIS_WINDOWS = 8, EMOJIS_JOYPIXELS = 9, 
       EMOJIS_OPENMOJI = 10, EMOJIS_ZOOM = 11 };
enum { EMPL_STRETCH = 0, EMPL_ALIGN = 1, EMPL_MIDDLE = 2, EMPL_FULL = 3 };

// Colour values.

typedef uint colour;
typedef struct { colour fg, bg; } colour_pair;

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
  colour fg_colour, bold_colour, blink_colour, bg_colour, cursor_colour;
  colour tek_fg_colour, tek_bg_colour, tek_cursor_colour;
  colour tek_write_thru_colour, tek_defocused_colour;
  colour tab_fg_colour, tab_bg_colour;
  int tek_glow;
  int tek_strap;
  colour underl_colour, hover_colour;
  int disp_space, disp_clear, disp_tab;
  bool underl_manual;
  colour sel_fg_colour, sel_bg_colour;
  colour search_fg_colour, search_bg_colour, search_current_colour;
  wstring theme_file;
  wstring background;
  string colour_scheme;
  char transparency;
  bool blurred;
  bool opaque_when_focused;
  char cursor_type;
  bool cursor_blinks;
  // Text
  font_spec font;
  font_spec fontfams[12];
  wstring font_choice;
  wstring font_sample;
  bool show_hidden_fonts;
  char font_smoothing;
  char font_render;
  bool bold_as_font;
  bool bold_as_colour;
  bool allow_blinking;
  string locale;
  string charset;
  char charwidth;
  bool old_locale;
  int fontmenu;
  wstring tek_font;
  // Keys
  bool backspace_sends_bs;
  bool delete_sends_del;
  bool ctrl_alt_is_altgr;
  bool altgr_is_alt;
  int ctrl_alt_delay_altgr;
  bool old_altgr_detection;
  int old_modify_keys;
  int format_other_keys;
  bool auto_repeat;
  int external_hotkeys;
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
  wstring key_commands;
  int manage_leds;
  bool enable_remap_ctrls;
  bool old_keyfuncs_keypad;
  // Mouse
  bool clicks_place_cursor;
  char middle_click_action;
  char right_click_action;
  int opening_clicks;
  char opening_mod;
  bool zoom_mouse;
  char clicks_target_app;
  char click_target_mod;
  bool hide_mouse;
  bool elastic_mouse;
  int lines_per_notch;
  wstring mouse_pointer;
  wstring appmouse_pointer;
  // Selection
  bool input_clears_selection;
  bool copy_on_select;
  bool copy_tabs;
  bool copy_as_rtf;
  char copy_as_html;
  wstring copy_as_rtf_font;
  int copy_as_rtf_font_size;
  bool trim_selection;
  bool allow_set_selection;
  int selection_show_size;
  // Window
  int cols, rows;
  bool rewrap_on_resize;
  int scrollback_lines;
  int max_scrollback_lines;
  char scrollbar;
  char scroll_mod;
  bool pgupdn_scroll;
  wstring lang;
  wstring search_bar;
  int search_context;
  // Terminal
  string term;
  wstring answerback;
  int wrap_tab;
  bool old_wrapmodes;
  bool enable_deccolm_init;
  int bell_type;
  wstring bell_file[7];
  int bell_freq;
  int bell_len;
  bool bell_flash;   // xterm: visualBell
  int bell_flash_style;
  bool bell_taskbar; // xterm: bellIsUrgent
  bool bell_popup;   // xterm: popOnBell
  int bell_interval;
  int play_tone;
  wstring printer;
  bool confirm_exit;
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
  bool erase_to_scrollback;
  int display_speedup;
  string suppress_sgr;
  string suppress_dec;
  string suppress_win;
  string suppress_osc;
  string suppress_nrc;
  string suppress_wheel;
  string filter_paste;
  int bracketed_paste_split;
  int suspbuf_max;
  int printable_controls;
  int char_narrowing;
  char emojis;
  char emoji_placement;
  wstring save_filename;
  wstring app_id;
  wstring app_name;
  wstring app_launch_cmd;
  wstring drop_commands;
  wstring exit_commands;
  wstring user_commands;
  wstring ctx_user_commands;
  wstring sys_user_commands;
  wstring user_commands_path;
  wstring session_commands;
  wstring task_commands;
  int conpty_support;
  bool login_from_shortcut;
  string menu_mouse;
  string menu_ctrlmouse;
  string menu_altmouse;
  string menu_menu;
  string menu_ctrlmenu;
  string menu_title_ctrl_l;
  string menu_title_ctrl_r;
  int geom_sync;
  int tabbar;
  int new_tabs;
  int col_spacing, row_spacing;
  int auto_leading;
  int padding;
  int ligatures;
  int ligatures_support;
  int handle_dpichanged;
  int check_version_update;
  string word_chars;
  string word_chars_excl;
  colour ime_cursor_colour;
  colour_pair ansi_colours[16];
  wstring sixel_clip_char;
  bool short_long_opts;
  bool bold_as_special;
  bool hover_title;
  char progress_bar;
  int progress_scan;
  int baud;
  int bloom;
  wstring options_font;
  int options_fontsize;
  string old_options;
  bool dim_margins;
  bool status_line;
  bool old_xbuttons;
  // Legacy
  bool use_system_colours;
  bool old_bold;
} config;


typedef void (* str_fn)(wchar *);

extern string config_dir;
extern config cfg, new_cfg, file_cfg;

extern void init_config(void);
extern void list_fonts(bool report);
extern void load_config(string filename, int to_save);
extern void load_theme(wstring theme);
extern char * get_resource_file(wstring sub, wstring res, bool towrite);
extern void handle_file_resources(wstring pattern, str_fn fnh);
extern void load_scheme(string colour_scheme);
extern void set_arg_option(string name, string val);
extern void parse_arg_option(string);
extern void remember_arg(string);
extern void finish_config(void);
extern void copy_config(char * tag, config * dst, const config * src);
extern void apply_config(bool save);
extern wchar * getregstr(HKEY key, wstring subkey, wstring attribute);
extern uint getregval(HKEY key, wstring subkey, wstring attribute);
extern char * save_filename(char * suf);
// In a configuration parameter list, map tag to value
extern char * matchconf(char * conf, char * item);

#endif
