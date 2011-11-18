// std.c (part of mintty)
// Copyright 2010-11 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

void
strset(string *sp, string s)
{
  uint size = strlen(s) + 1;
  *sp = memcpy(renewn((char *)*sp, size), s, size);
}

#if CYGWIN_VERSION_API_MINOR < 70

int
vasprintf(char **buf, const char *fmt, va_list va)
{
  va_list va2;
  va_copy(va2, va);
  int len = vsnprintf(0, 0, fmt, va2);
  va_end(va2);
  if (len > 0) {
    *buf = malloc(len + 1);
    if (*buf)
      vsnprintf(*buf, len + 1, fmt, va);
  }
  else
    *buf = 0;
  return len;
}

int
asprintf(char **buf, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  int len = vasprintf(buf, fmt, va);
  va_end(va);
  return len;
}

#endif

char *
asform(const char *fmt, ...)
{
  char *s = 0;
  va_list va;
  va_start(va, fmt);
  vasprintf(&s, fmt, va);
  va_end(va);
  return s;
}


#if CYGWIN_VERSION_API_MINOR < 74
int iswalnum(wint_t wc) { return wc < 0x100 && isalnum(wc); }
int iswalpha(wint_t wc) { return wc < 0x100 && isalpha(wc); }
int iswspace(wint_t wc) { return wc < 0x100 && isspace(wc); }
#endif


#if CYGWIN_VERSION_API_MINOR < 91

/* Copyright (C) 2002 by  Red Hat, Incorporated. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * is freely granted, provided that this notice is preserved.
 */

int
argz_create(char *const argv[], char **argz, size_t *argz_len)
{
  int argc = 0;
  int i = 0;
  int len = 0;
  char *iter;

  *argz_len = 0;

  if (*argv == NULL)
    {
      *argz = NULL;
      return 0;
    }

  while (argv[argc])
    {
      *argz_len += (strlen(argv[argc]) + 1);
      argc++;
    }

  /* There are argc strings to copy into argz. */
  if(!(*argz = (char *)malloc(*argz_len)))
    return ENOMEM;

  iter = *argz;
  for(i = 0; i < argc; i++)
    {
      len = strlen(argv[i]) + 1;
      memcpy(iter, argv[i], len);
      iter += len;
    }
  return 0;
}

void
argz_stringify(char *argz, size_t argz_len, int sep)
{
  size_t i;

  /* len includes trailing \0, which we don't want to replace. */
  if (argz_len > 1)
    for (i = 0; i < argz_len - 1; i++)
      {
        if (argz[i] == '\0')
          argz[i] = sep;
      }
}
#endif

#if CYGWIN_VERSION_API_MINOR < 93

/*-
 * Copyright (c) 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * CV 2003-09-10: Cygwin specific changes applied.  Code simplified just
 *                for Cygwin alone.
 */

#include <fcntl.h>
#include <sys/termios.h>
#include <sys/ioctl.h>

#define TTY_NAME_MAX 32

int
login_tty(int fd)
{
  char *fdname;
  int newfd;

  if (setsid () == -1)
    return -1;
  if ((fdname = ttyname (fd)))
    {
      if (fd != STDIN_FILENO)
        close (STDIN_FILENO);
      if (fd != STDOUT_FILENO)
        close (STDOUT_FILENO);
      if (fd != STDERR_FILENO)
        close (STDERR_FILENO);
      newfd = open (fdname, O_RDWR);
      close (newfd);
    }
  dup2 (fd, STDIN_FILENO);
  dup2 (fd, STDOUT_FILENO);
  dup2 (fd, STDERR_FILENO);
  if (fd > 2)
    close (fd);
  return 0;
}

int
openpty(int *amaster, int *aslave, char *name,
        const struct termios *termp, const struct winsize *winp)
{
  int master, slave;
  char pts[TTY_NAME_MAX];

  if ((master = open ("/dev/ptmx", O_RDWR | O_NOCTTY)) >= 0)
    {
      grantpt (master);
      unlockpt (master);
      strcpy (pts, ptsname (master));
      if ((slave = open (pts, O_RDWR | O_NOCTTY)) >= 0)
        {
          if (amaster)
            *amaster = master;
          if (aslave)
            *aslave = slave;
          if (name)
            strcpy (name, pts);
          if (termp)
            tcsetattr (slave, TCSAFLUSH, termp);
          if (winp)
            ioctl (master, TIOCSWINSZ, (char *) winp);
          return 0;
        }
      close (master);
    }
  errno = ENOENT;
  return -1;
}

int
forkpty(int *amaster, char *name,
        const struct termios *termp, const struct winsize *winp)
{
  int master, slave, pid;

  if (openpty (&master, &slave, name, termp, winp) == -1)
    return -1;
  switch (pid = fork ())
    {
      case -1:
        return -1;
      case 0:
        close (master);
        login_tty (slave);
        return 0;
    }
  if (amaster)
    *amaster = master;
  close (slave);
  return pid;
}

#endif
