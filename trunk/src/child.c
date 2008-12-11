// child.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "child.h"

#include "term.h"
#include "config.h"

#include <pwd.h>
#include <unistd.h>
#include <pty.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <argz.h>
#include <utmp.h>
#include <dirent.h>

#include <winbase.h>

int child_exitcode;
HANDLE child_event;
static HANDLE proc_event;
static pid_t pid;
static int fd;
static int read_len;
static char read_buf[4096];
static struct utmp ut;

void
child_proc(void)
{
  if (read_len > 0) {
    term_write(read_buf, read_len);
    SetEvent(proc_event);
  }
  else if (child_exitcode == 0)
    exit(0);
}

static DWORD WINAPI
child_read_thread(LPVOID unused(param))
{
  while ((read_len = read(fd, read_buf, sizeof read_buf)) > 0) {
    SetEvent(child_event);
    WaitForSingleObject(proc_event, INFINITE);
  };
  int status;
  while (wait(&status) != pid);
  child_exitcode = WEXITSTATUS(status);
  pid = 0;
  logout(ut.ut_line);
  SetEvent(child_event);
  for (;;) wait(0);
  return 0;
}

static void
signal_handler(int unused(sig))
{
  if (pid)
    kill(pid, SIGHUP);
  else
    exit(child_exitcode);
}
  
char *
child_create(char *argv[], struct winsize *winp)
{
  struct passwd *pw = getpwuid(getuid());
  if (!*argv) {
    char *shell = (pw ? pw->pw_shell : 0) ?: "/bin/sh"; 
    argv = (char *[]){shell, 0};
  }
  
  // Create the child process and pseudo terminal.
  pid = forkpty(&fd, 0, 0, winp);
  if (pid == -1) { // Fork failed.
    char *msg = strdup(strerror(errno));
    term_write("forkpty: ", 16);
    term_write(msg, strlen(msg));
    free(msg);
    pid = 0;
  }
  else if (pid == 0) { // Child process.
    struct termios attr;
    tcgetattr(0, &attr);
    attr.c_cc[VERASE] = cfg.backspace_sends_del ? 0x7F : '\b';
    attr.c_cc[VDISCARD] = 0;
    tcsetattr(0, TCSANOW, &attr);
    setenv("TERM", "xterm", 1);
    execvp(*argv, argv);

    // If we get here, exec failed.
    char *msg = strdup(strerror(errno));
    write(STDERR_FILENO, "exec: ", 6);
    write(STDERR_FILENO, *argv, strlen(*argv));
    write(STDERR_FILENO, ": ", 2);
    write(STDERR_FILENO, msg, strlen(msg));
    free(msg);
    exit(1);
  }
  else { // Parent process.
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    
    ut.ut_type = USER_PROCESS;
    ut.ut_pid = pid;
    ut.ut_time = time(0);
    char *dev = ptsname(fd);
    if (dev) {
      if (strncmp(dev, "/dev/", 5) == 0)
        dev += 5;
      strncpy(ut.ut_line, dev ?: "?", sizeof ut.ut_line);
      if (strncmp(dev, "pty", 3) == 0 || strncmp(dev, "tty", 3) == 0)
        dev += 3;
      strncpy(ut.ut_id, dev ?: "?", sizeof ut.ut_id);      
    }
    strncpy(ut.ut_user, (pw ? pw->pw_name : 0) ?: "?", sizeof ut.ut_user);
    login(&ut);
    
    child_event = CreateEvent(null, false, false, null);
    proc_event = CreateEvent(null, false, false, null);
    CreateThread(null, 0, child_read_thread, 0, 0, 0);
  }
  
  // Return child command line for window title.
  char *argz;
  size_t argz_len;
  argz_create(argv, &argz, &argz_len);
  argz_stringify(argz, argz_len, ' ');
  return argz;
}

void
child_kill(void)
{ kill(pid, SIGHUP); }

void
child_write(const char *buf, int len)
{ 
  if (pid)
    write(fd, buf, len); 
  else
    exit(child_exitcode);
}

void
child_resize(struct winsize *winp)
{ ioctl(fd, TIOCSWINSZ, winp); }

bool
child_is_parent(void)
{
  DIR *d = opendir("/proc");
  if (!d)
    return false;
  bool res = false;
  struct dirent *e;
  char fn[18] = "/proc/";
  while ((e = readdir(d))) {
    char *pn = e->d_name;
    if (isdigit(*pn) && strlen(pn) <= 6) {
      snprintf(fn + 6, 12, "%s/ppid", pn);
      FILE *f = fopen(fn, "r");
      if (!f)
        continue;
      pid_t ppid = 0;
      fscanf(f, "%u", &ppid);
      fclose(f);
      if (ppid == pid) {
        res = true;
        break;
      }
    }
  }
  closedir(d);
  return res;
}
