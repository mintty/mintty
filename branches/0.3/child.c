// child.c (part of MinTTY)
// Copyright 2008 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "child.h"

#include "term.h"
#include "config.h"

#include <pwd.h>
#include <pty.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <argz.h>
#include <utmp.h>
#include <dirent.h>

#include <winbase.h>

HANDLE child_event;
static HANDLE proc_event;
static pid_t pid;
static int status;
static int fd;
static int read_len;
static char read_buf[4096];
static struct utmp ut;
static char *name;

static const int ERROR_SIGS =
  1<<SIGILL | 1<<SIGTRAP | 1<<SIGABRT | 1<<SIGFPE | 
  1<<SIGBUS | 1<<SIGSEGV | 1<<SIGPIPE | 1<<SIGSYS; 

static DWORD WINAPI
read_thread(LPVOID unused(param))
{
  while ((read_len = read(fd, read_buf, sizeof read_buf)) > 0) {
    SetEvent(child_event);
    WaitForSingleObject(proc_event, INFINITE);
  };
  return 0;
}

static DWORD WINAPI
wait_thread(LPVOID unused(param))
{
  while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
    ;
  pid = 0;
  SetEvent(child_event);
  return 0;
}

bool
child_proc(void)
{
  if (read_len > 0) {
    term_write(read_buf, read_len);
    read_len = 0;
    SetEvent(proc_event);
  }
  if (pid == 0) {
    logout(ut.ut_line);
    int l = -1;
    char *s; 
    if (WIFEXITED(status)) {
      status = WEXITSTATUS(status);
      if (status == 0)
        exit(0);
      else
        l = asprintf(&s, "\r\n%s exited with status %i\r\n", name, status); 
    }
    else if (WIFSIGNALED(status)) {
      int sig = WTERMSIG(status);
      if ((ERROR_SIGS & 1<<sig) == 0)
        exit(0);
      else
        l = asprintf(&s, "\r\n%s terminated: %s\r\n", name, strsignal(sig));
    }
    if (l != -1) {
      term_write(s, l);
      free(s);
    }
    return true;
  }
  return false;
}

static void
signal_handler(int unused(sig))
{ 
  if (pid)
    kill(pid, SIGHUP);
  exit(0);
}

char *
child_create(char *argv[], struct winsize *winp)
{
  struct passwd *pw = getpwuid(getuid());
  if (!*argv) {
    char *shell = (pw ? pw->pw_shell : 0) ?: "/bin/sh"; 
    argv = (char *[]){shell, 0};
  }
  
  // Remember the child's name.
  name = *argv;
  
  // Create the child process and pseudo terminal.
  pid = forkpty(&fd, 0, 0, winp);
  if (pid == -1) { // Fork failed.
    char *msg = strdup(strerror(errno));
    term_write("forkpty: ", 8);
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
    CreateThread(null, 0, read_thread, 0, 0, 0);
    CreateThread(null, 0, wait_thread, 0, 0, 0);
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
{ 
  static bool huped = false;
  if (!huped) {
    kill(pid, SIGHUP);
    huped = true;
  }
  else {
    // Use brute force and exit.
    kill(pid, SIGKILL);
    exit(0);
  }
}

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

void
child_write(const char *buf, int len)
{ 
  if (pid)
    write(fd, buf, len); 
  else
    exit(0);
}

void
child_resize(struct winsize *winp)
{ ioctl(fd, TIOCSWINSZ, winp); }

