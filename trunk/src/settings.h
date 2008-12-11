#ifndef STORAGE_H
#define STORAGE_H

#include "platform.h"

typedef struct {
  char name[64];
  int isbold;
  int height;
  int charset;
} font_spec;

typedef struct { ubyte red, green, blue; } colour;

char *open_settings_w(char *filename);
void write_string_setting(const char *key, const char *value);
void write_int_setting(const char *key, int value);
void write_font_setting(const char *key, font_spec);
void write_colour_setting(const char *key, colour);
void close_settings_w(void);

void open_settings_r(char *filename);
void read_string_setting(const char *key, const char *def, char *, int len);
void read_int_setting(const char *key, int def, int *);
void read_font_setting(const char *key, font_spec def, font_spec *);
void read_colour_setting(const char *key, colour def, colour *);
void close_settings_r(void);

#endif
