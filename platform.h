#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __CYGWIN__
 #include <cygwin/version.h>
 #if CYGWIN_VERSION_DLL_MAJOR >= 1007
  #define NEEDS_WIN7_CONSOLE_WORKAROUND 0
  #define HAS_LOCALES 1
  #define HAS_WCWIDTH 1
 #else
  #define NEEDS_WIN7_CONSOLE_WORKAROUND 1
  #define HAS_LOCALES 0
  #define HAS_WCWIDTH 0
  typedef uint32_t xchar;
  int xcwidth(xchar c);
 #endif
#else
 #error Platform not configured.
#endif

// Colours

typedef uint32 colour;

static inline colour
make_colour(uint8 r, uint8 g, uint8 b) { return r | g << 8 | b << 16; }

static inline uint8 red(colour c) { return c & 0xff; }
static inline uint8 green(colour c) { return c >> 8 & 0xff; }
static inline uint8 blue(colour c) { return c >> 16 & 0xff; }

int get_tick_count(void);
int cursor_blink_ticks(void);

#endif
