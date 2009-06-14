#ifndef STORAGE_H
#define STORAGE_H

#include "platform.h"

char *open_settings_w(const char *filename);
void write_string_setting(const char *key, const char *value);
void write_int_setting(const char *key, int value);
void write_colour_setting(const char *key, colour);
void close_settings_w(void);

void open_settings_r(const char *filename);
void read_string_setting(const char *key, char *, int len, const char *def);
void read_int_setting(const char *key, int *, int def);
void read_colour_setting(const char *key, colour *, colour def);
void close_settings_r(void);

#endif
