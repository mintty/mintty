#ifndef CHILD_H
#define CHILD_H

#include <sys/termios.h>

extern bool clone_size_token;

extern void child_create(char * argv[], struct winsize * winp);
extern void child_proc(void);
extern void child_kill(bool point_blank);
extern void child_write(const char *, uint len);
extern void child_printf(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
extern void child_send(const char *, uint len);
extern void child_sendw(const wchar *, uint len);
extern void child_resize(struct winsize * winp);
extern bool child_is_alive(void);
extern bool child_is_parent(void);
extern char * child_tty(void);
extern int foreground_pid(void);
extern wstring child_conv_path(wstring);
extern void child_fork(int argc, char * argv[], int moni);
extern void child_set_fork_dir(char *);

#endif
