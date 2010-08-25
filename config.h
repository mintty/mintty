#ifndef CONFIG_H
#define CONFIG_H

#include "term.h"

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
  int transparency;
  bool opaque_when_focused;
  int cursor_type;
  bool cursor_blinks;
  // Text
  font_spec font;
  int font_quality;
  bool bold_as_colour;
  bool allow_blinking;
  char locale[32];
  char charset[32];
  // Keys
  bool backspace_sends_bs;
  bool ctrl_alt_is_altgr;
  bool window_shortcuts;
  bool zoom_shortcuts;
  bool switch_shortcuts;
  int scroll_mod;
  bool pgupdn_scroll;
  // Mouse
  bool copy_on_select;
  bool copy_as_rtf;
  bool clicks_place_cursor;
  int right_click_action;
  int clicks_target_app;
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
  int scrollbar;
  int scrollback_lines;
  bool confirm_exit;
  // Hidden
  int col_spacing, row_spacing;
  char word_chars[32];
  bool use_system_colours;
  colour ansi_colours[16];
} config;

extern config cfg, new_cfg;

int parse_option(char *option);
void load_config(char *filename);
void finish_config(void);

#endif
