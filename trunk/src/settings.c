// settings.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "settings.h"

#include "tree234.h"
#include <sys/stat.h>

#define FILENAME ".mintty"

static FILE *file;

char *
open_settings_w(void)
{
  char *filename;
  asprintf(&filename, "%s/" FILENAME, getenv("HOME"));
  file = fopen(filename, "w");
  char *errmsg = NULL;
  if (!file)
    asprintf(&errmsg, "Unable to create %s: %s", filename, strerror(errno));
  free(filename);
  return errmsg;
}

void
write_string_setting(const char *key, const char *value)
{
  fprintf(file, "%s=%s\n", key, value);
}

void
write_int_setting(const char *key, int value)
{
  fprintf(file, "%s=%d\n", key, value);
}

void
write_font_setting(const char *key, font_spec font)
{
  write_string_setting(key, font.name);
  char key2[16];
  snprintf(key2, sizeof key2, "%sIsBold", key);
  write_int_setting(key2, font.isbold);
  snprintf(key2, sizeof key2, "%sHeight", key);
  write_int_setting(key2, font.height);
  snprintf(key2, sizeof key2, "%sCharset", key);
  write_int_setting(key2, font.charset);
}

void
write_colour_setting(const char *key, colour value)
{
  fprintf(file, "%s=%u,%u,%u\n", key, value.red, value.green, value.blue);
}

void 
close_settings_w(void)
{
  fclose(file);
}

typedef struct {
  const char *key;
  const char *value;
} keyval;

static int
keyvalcmp(void *a, void *b)
{
  return strcmp(((keyval *)a)->key, ((keyval *)b)->key);
}

static tree234 *tree;

void
open_settings_r(void)
{
  char *filename;
  asprintf(&filename, "%s/" FILENAME, getenv("HOME"));
  FILE *file = fopen(filename, "r");
  free(filename);
  if (!file) {
    tree = 0;
    return;
  }

  tree = newtree234(keyvalcmp);

  char *line;
  size_t len;
  while (line = 0, len = 0, __getline(&line, &len, file) != -1) {
    char *value = strchr(line, '=');
    if (!value)
      continue;
    *value++ = '\0';
    value[strcspn(value, "\r\n")] = '\0';       /* trim trailing NL */
    keyval *kv = new(keyval);
    kv->key = strdup(line);
    kv->value = strdup(value);
    add234(tree, kv);
    free(line);
  }
  fclose(file);
}

static const char *
lookup_val(const char *key)
{
  keyval *kv;
  if (tree && (kv = find234(tree, &(keyval){key, 0}, 0)))
    return kv->value;
  else
    return 0;
}
  
void
read_string_setting(const char *key, const char *def, char *res, int len)
{
  const char *val = lookup_val(key) ?: def;
  strncpy(res, val, len);
  res[len - 1] = 0;
}

void
read_int_setting(const char *key, int def, int *res_p)
{
  const char *val = lookup_val(key);
  *res_p = val ? atoi(val) : def;
}

void
read_font_setting(const char *key, font_spec def, font_spec *res_p)
{
  read_string_setting(key, def.name, res_p->name, sizeof res_p->name);
  char key2[16];
  snprintf(key2, sizeof key2, "%sIsBold", key);
  read_int_setting(key2, def.isbold, &res_p->isbold);
  snprintf(key2, sizeof key2, "%sHeight", key);
  read_int_setting(key2, def.height, &res_p->height);
  snprintf(key2, sizeof key2, "%sCharset", key);
  read_int_setting(key2, def.charset, &res_p->charset);
}

void
read_colour_setting(const char *key, colour def, colour *res)
{
  const char *val = lookup_val(key); 
  if (val) {
    int r, g, b;
    if (sscanf(val, "%d,%d,%d", &r, &g, &b) == 3) {
      *res = (colour){r, g, b};
      return;
    }
  }
  *res = def;
}


void
close_settings_r(void)
{
  if (tree) {
    keyval *kv;
    while ((kv = index234(tree, 0))) {
      del234(tree, kv);
      free((char *) kv->key);
      free((char *) kv->value);
      free(kv);
    }
    freetree234(tree);
    tree = 0;
  }
}
