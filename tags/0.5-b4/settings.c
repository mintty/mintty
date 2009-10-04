// settings.c (part of mintty)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "settings.h"

#include "tree234.h"
#include <sys/stat.h>

#define FILENAME ".mintty"

static FILE *file;

char *
open_settings_w(const char *filename)
{
  file = fopen(filename, "w");
  char *errmsg = NULL;
  if (!file)
    asprintf(&errmsg, "Unable to create %s: %s", filename, strerror(errno));
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
write_colour_setting(const char *key, colour c)
{
  fprintf(file, "%s=%u,%u,%u\n", key, red(c), green(c), blue(c));
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
open_settings_r(const char *filename)
{
  FILE *file = fopen(filename, "r");
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
read_string_setting(const char *key, char *res, int len, const char *def)
{
  snprintf(res, len, lookup_val(key) ?: def);
}

void
read_int_setting(const char *key, int *res_p, int def)
{
  const char *val = lookup_val(key);
  *res_p = val ? atoi(val) : def;
}

void
read_colour_setting(const char *key, colour *res_p, colour def)
{
  const char *val = lookup_val(key); 
  if (val) {
    uint r, g, b;
    if (sscanf(val, "%u,%u,%u", &r, &g, &b) == 3) {
      *res_p = make_colour(r, g, b);
      return;
    }
  }
  *res_p = def;
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
