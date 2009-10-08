#ifndef CHILD_H
#define CHILD_H

#include <sys/termios.h>

char *child_create(char *argv[], const char *lang, struct winsize *winp);
void child_kill(void);
void child_write(const char *, int len);
void child_resize(struct winsize *winp);
bool child_proc(void);
bool child_is_parent(void);
void child_open(char *, int len);
extern HANDLE child_event;

#endif
