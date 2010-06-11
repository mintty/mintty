#ifndef CHILD_H
#define CHILD_H

#include <sys/termios.h>

no_return child_create(char *argv[], char *title, struct winsize *winp);
void child_kill(bool point_blank);
void child_write(const char *, uint len);
void child_send(const char *, uint len);
void child_sendw(const wchar *, uint len);
void child_resize(struct winsize *winp);
bool child_is_parent(void);
const wchar *child_conv_path(const wchar *);
void child_fork(char *argv[]);

#endif
