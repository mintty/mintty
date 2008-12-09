#ifndef STD_H
#define STD_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#define WINVER 0x500
#define _WIN32_WINNT WINVER
#define _WIN32_IE WINVER

#include <windef.h>

#define always_inline __attribute__((always_inline)) inline
#define unused(arg) unused_##arg __attribute__((unused))

typedef wchar_t wchar;

typedef signed char byte;
typedef unsigned char ubyte;
typedef unsigned short ushort;
typedef unsigned int uint;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

#define C(c) ((c) & 0x1f)

#define null ((void *) 0)

#define lenof(x) (sizeof(x) / sizeof(*(x)))

#define new(type) ((type *) malloc(sizeof(type)))
#define newn(type, n) ((type *) calloc(n, sizeof(type)))
#define renewn(p, n) ((typeof(p)) realloc(p, sizeof(*p) * n))

#ifndef min
#define min(a, b) ((a) <= (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

#define when break; case
#define or : case
#define otherwise break; default

#ifdef TRACE
#define trace(xs...) \
    printf("%s:%u:%s:", __FILE__, __LINE__, __func__); \
    printf(" " xs); \
    putchar('\n')
#else
#define trace(f, xs...) {}
#endif

#endif
