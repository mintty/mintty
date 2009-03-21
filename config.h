#ifndef CONFIG_H
#define CONFIG_H

#include "settings.h"

char *config_filename;

typedef struct {
  char name[64];
  int isbold;
  int height;
  int charset;
} font_spec;

static const char ANSWERBACK[] = "xterm";
enum { SAVELINES = 65535 };

/* Bell type */
enum { BELL_DISABLED, BELL_SOUND, BELL_VISUAL };

/* Taskbar flashing indication on bell (cfg.bell_ind) */
enum { B_IND_DISABLED, B_IND_FLASH, B_IND_STEADY };

enum { BELLOVL_T = 5, BELLOVL_S = 2, BELLOVL_N = 5 };

enum { CUR_BLOCK, CUR_UNDERLINE, CUR_LINE };

enum { FQ_DEFAULT, FQ_ANTIALIASED, FQ_NONANTIALIASED, FQ_CLEARTYPE };

enum { RC_SHOWMENU, RC_PASTE, RC_EXTEND };

typedef struct {
  int copy_on_select;
  int escape_sends_fs;
  int backspace_sends_del;
  int alt_sends_esc;
  int cursor_type;
  int cursor_blinks;
  int bell_type;
  int bell_ind;
  int scrollbar;
  int window_shortcuts;
  int scroll_mod;
  int click_targets_app;
  int click_target_mod;
  int right_click_action;
  int click_moves_cmd_cursor;
  int allow_blinking;
  int rows, cols;
  char printer[128];
 /* Colour options */
  int bold_as_bright;
  colour fg_colour, bg_colour, cursor_colour;
  int transparency;
  int opaque_when_focused;
 /* translations */
  char codepage[128];
 /* fonts */
  font_spec font;
  int font_quality;
} config;

extern config cfg;

void load_config(void);
char *save_config(void);

#endif
