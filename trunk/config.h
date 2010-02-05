#ifndef CONFIG_H
#define CONFIG_H

typedef uint32 colour;

static inline colour
make_colour(uint8 r, uint8 g, uint8 b) { return r | g << 8 | b << 16; }

static inline uint8 red(colour c) { return c & 0xff; }
static inline uint8 green(colour c) { return c >> 8 & 0xff; }
static inline uint8 blue(colour c) { return c >> 16 & 0xff; }

typedef enum { HOLD_NEVER, HOLD_ALWAYS, HOLD_ERROR } hold_t;
extern hold_t hold;

extern const char *log_file;
extern bool utmp_enabled;

typedef struct {
  char name[64];
  bool isbold;
  int size;
} font_spec;

enum { CUR_BLOCK, CUR_UNDERSCORE, CUR_LINE };

enum { FQ_DEFAULT, FQ_ANTIALIASED, FQ_NONANTIALIASED, FQ_CLEARTYPE };

enum { RC_SHOWMENU, RC_PASTE, RC_EXTEND };

typedef struct {
  // Looks
  colour fg_colour, bg_colour, cursor_colour;
  bool use_system_colours;
  int transparency;
  bool opaque_when_focused;
  int cursor_type;
  bool cursor_blinks;
  // Text
  font_spec font;
  int font_quality;
  bool bold_as_bright;
  bool allow_blinking;
  char locale[16];
  char charset[16];
  // Keys
  bool backspace_sends_bs;
  bool alt_sends_esc;
  bool ctrl_alt_is_altgr;
  bool window_shortcuts;
  bool zoom_shortcuts;
  int scroll_mod;
  // Mouse
  bool copy_on_select;
  bool clicks_place_cursor;
  int right_click_action;
  bool clicks_target_app;
  int click_target_mod;
  // Output
  char printer[64];
  bool bell_sound;
  bool bell_flash;
  bool bell_taskbar;
  char term[32];
  char answerback[80];
  // Window
  int cols, rows;
  bool scrollbar;
  bool alt_screen_scroll;
  int scrollback_lines;
  bool confirm_exit;
  // Hidden
  int line_spacing;
} config;

extern config cfg, new_cfg;

int parse_option(char *option);
void load_config(char *filename);

#endif
