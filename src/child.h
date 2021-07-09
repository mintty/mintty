#ifndef CHILD_H
#define CHILD_H

#include <termios.h>

extern string child_dir;

extern void child_update_charset(void);
extern void child_create(char * argv[], struct winsize * winp);
extern void toggle_logging(void);
extern bool logging;
extern void child_proc(void);
extern void child_kill(bool point_blank);
extern void child_write(const char *, uint len);
extern void child_break(void);
extern void child_printf(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
extern void child_send(const char *, uint len);
extern void child_sendw(const wchar *, uint len);
extern void child_resize(struct winsize * winp);
extern bool child_is_alive(void);
extern bool child_is_parent(void);
extern wchar * grandchild_process_list(void);
extern char * child_tty(void);
extern char * foreground_prog(void);  // to be free()d
extern void user_command(wstring commands, int n);
extern wstring child_conv_path(wstring, bool adjust_dir);
extern void child_fork(int argc, char * argv[], int moni, bool config_size, bool in_cwd);
extern void child_set_fork_dir(char *);
extern void setenvi(char * env, int val);
extern void child_launch(int n, int argc, char * argv[], int moni);

#endif
