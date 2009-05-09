#ifndef CHILD_H
#define CHILD_H

#include <sys/termios.h>

typedef enum { HOLD_NEVER, HOLD_ALWAYS, HOLD_ERROR } hold_t;

char *child_create(char *argv[], struct winsize *winp,
                   const char *log_file, bool log_utmp);
void child_kill(void);
void child_write(const char *, int len);
void child_resize(struct winsize *winp);
bool child_proc(hold_t hold);
bool child_is_parent(void);
extern HANDLE child_event;

#endif
