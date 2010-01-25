// child.c (part of mintty)
// Copyright 2008-09 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "child.h"

#include "term.h"
#include "config.h"
#include "charset.h"

#include <pwd.h>
#include <pty.h>
#include <fcntl.h>
#include <argz.h>
#include <utmp.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/cygwin.h>

#include <winbase.h>

#if CYGWIN_VERSION_DLL_MAJOR < 1007
#include <winnls.h>
#include <wincon.h>
#include <wingdi.h>
#include <winuser.h>
#endif

extern HWND wnd;

HANDLE child_event;

static HANDLE proc_event;
static pid_t pid;
static int status;
static int pty_fd = -1, log_fd = -1;
static int read_len;
static char read_buf[4096];
static struct utmp ut;
static char *child_name;
static bool killed;

static sigset_t term_sigs;

static void *
signal_thread(void *unused(arg))
{
  int sig;
  sigwait(&term_sigs, &sig);
  if (pid)
    kill(pid, SIGHUP);
  exit(0);
}

static void *
read_thread(void *unused(arg))
{
  while ((read_len = read(pty_fd, read_buf, sizeof read_buf)) > 0) {
    SetEvent(child_event);
    WaitForSingleObject(proc_event, INFINITE);
  };
  close(pty_fd);
  pty_fd = -1;
  return 0;
}

static void *
wait_thread(void *unused(arg))
{
  while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
    ;
  Sleep(100); // Give any ongoing output some time to finish.
  pid = 0;
  SetEvent(child_event);
  return 0;
}

bool
child_proc(void)
{
  if (read_len > 0) {
    term_write(read_buf, read_len);
    write(log_fd, read_buf, read_len);
    read_len = 0;
    SetEvent(proc_event);
  }

  if (pid)
    return false;

  logout(ut.ut_line);

  // No point hanging around if the user wants us dead.
  if (killed || hold == HOLD_NEVER)
    exit(0);
  
  if (hold == HOLD_ALWAYS)
    return false;

  // Display a message if the child process died with an error. 
  int l = -1;
  char *s; 
  if (WIFEXITED(status)) {
    status = WEXITSTATUS(status);
    if (status == 0)
      exit(0);
    else
      l = asprintf(&s, "\r\n%s exited with status %i\r\n", child_name, status); 
  }
  else if (WIFSIGNALED(status)) {
    int error_sigs =
      1<<SIGILL | 1<<SIGTRAP | 1<<SIGABRT | 1<<SIGFPE | 
      1<<SIGBUS | 1<<SIGSEGV | 1<<SIGPIPE | 1<<SIGSYS;
    int sig = WTERMSIG(status);
    if ((error_sigs & 1<<sig) == 0)
      exit(0);
    l = asprintf(&s, "\r\n%s terminated: %s\r\n", child_name, strsignal(sig));
  }
  if (l != -1) {
    term_write(s, l);
    term_write("Press any key to close\r\n", 24);
    free(s);
  }
  return true;
}

static void
error(char *action)
{
  char *msg;
  int len = asprintf(&msg, "Failed to %s: %s", action, strerror(errno));
  if (len > 0) {
    term_write(msg, len);
    free(msg);
  }
  pid = 0;
}

static int
nonstdfd(int fd)
{
  // Move file descriptor out of standard range if necessary.
  if (fd < 3) {
    int old_fd = fd;
    fd = fcntl(fd, F_DUPFD, 3);
    close(old_fd);
  }
  return fd;
}

char *
child_create(char *argv[], const char *lang, struct winsize *winp)
{
  struct passwd *pw = getpwuid(getuid());
  char *cmd; 
  if (*argv && (argv[1] || strcmp(*argv, "-") != 0))
    cmd = *argv;
  else {
    cmd = getenv("SHELL") ?: (pw ? pw->pw_shell : 0) ?: "/bin/sh";
    char *last_slash = strrchr(cmd, '/');
    char *name = last_slash ? last_slash + 1 : cmd;
    if (*argv)
      asprintf(&name, "-%s", name);
    argv = (char *[]){name, 0};
  }
  child_name = *argv;
  
  // Command line for window title.
  char *argz;
  size_t argz_len;
  argz_create(argv, &argz, &argz_len);
  argz_stringify(argz, argz_len, ' ');
  
  // Open log file if any
  if (log_file) {
    int fd = open(log_file, O_WRONLY | O_CREAT);
    if (fd == -1) {
      error("open log file");
      return argz;
    }
    log_fd = nonstdfd(fd);
  }

  // Create the child process and pseudo terminal.
  if ((pid = forkpty(&pty_fd, 0, 0, winp)) == -1)
    error("create child process");
  else if (pid == 0) { // Child process.
#if CYGWIN_VERSION_DLL_MAJOR < 1007
    // The Cygwin 1.5 DLL's trick of allocating a console on an invisible
    // "window station" no longer works on Windows 7 due to a bug that
    // Microsoft don't intend to fix anytime soon.
    // Hence, here's a hack that allocates a console for the child command
    // and hides it. Annoyingly the console window still flashes up briefly.
    // Cygwin 1.7 has a better workaround.
    DWORD win_version = GetVersion();
    win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
    struct utsname un;
    uname(&un);
    if (win_version >= 0x0601 && un.release[2] == '5') {
      if (AllocConsole()) {
        HMODULE kernel = LoadLibrary("kernel32");
        HWND (WINAPI *pGetConsoleWindow)(void) =
          (void *)GetProcAddress(kernel, "GetConsoleWindow");
        ShowWindowAsync(pGetConsoleWindow(), SW_HIDE);
      }
    }
#endif

    // Reset signals
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Mimick login's behavior by disabling the job control signals
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    setenv("TERM", cfg.term, true);
    
    if (lang) {
      unsetenv("LC_ALL");
      unsetenv("LC_CTYPE");
      setenv("LANG", lang, true);
    }

    // Set backspace keycode
    struct termios attr;
    tcgetattr(0, &attr);
    attr.c_cc[VERASE] = cfg.backspace_sends_bs ? '\b' : 0x7F;
    tcsetattr(0, TCSANOW, &attr);
    
    // Invoke command
    execvp(cmd, argv);

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
    pty_fd = nonstdfd(pty_fd);
    
    child_event = CreateEvent(null, false, false, null);
    proc_event = CreateEvent(null, false, false, null);
    
    sigemptyset(&term_sigs);
    sigaddset(&term_sigs, SIGHUP);
    sigaddset(&term_sigs, SIGINT);
    sigaddset(&term_sigs, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &term_sigs, null);
    
    pthread_t thread;
    pthread_create(&thread, 0, signal_thread, 0);
    pthread_create(&thread, 0, wait_thread, 0);
    pthread_create(&thread, 0, read_thread, 0);

    if (utmp_enabled) {
      ut.ut_type = USER_PROCESS;
      ut.ut_pid = pid;
      ut.ut_time = time(0);
      char *dev = ptsname(pty_fd);
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
    }
  }
  
  return argz;
}

void
child_kill(void)
{ 
  if (!killed) {
    // Tell the child nicely.
    kill(pid, SIGHUP);
    killed = true;
  }
  else {
    // Use brute force and head for the exit.
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
    if (isdigit((uchar)*pn) && strlen(pn) <= 6) {
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
  if (pty_fd >= 0)
    write(pty_fd, buf, len); 
  else if (!pid)
    exit(0);
}

void
child_resize(struct winsize *winp)
{ 
  if (pty_fd >= 0)
    ioctl(pty_fd, TIOCSWINSZ, winp);
}

const wchar *
child_conv_path(const wchar *wpath)
{
  int wlen = wcslen(wpath);
  int len = wlen * cs_cur_max;
  char path[len];
  len = cs_wcntombn(path, wpath, len, wlen);
  path[len] = 0;
  
  char *exp_path;  // expanded path
  if (*path == '~') {
    // Tilde expansion
    char *name = path + 1;
    char *rest = strchr(path, '/');
    if (rest)
      *rest++ = 0;
    else
      rest = "";
    struct passwd *pw = *name ? getpwnam(name) : getpwuid(getuid());
    char *home = pw ? pw->pw_dir : 0;
    if (home)
      asprintf(&exp_path, "%s/%s", home, rest);
    else
      exp_path = path;
  }
  else if (*path != '/' && pid) {
    // Relative path: prepend child process' working directory
    char proc_cwd[32];
    sprintf(proc_cwd, "/proc/%u/cwd", pid);
    char *cwd = realpath(proc_cwd, 0);
    asprintf(&exp_path, "%s/%s", cwd, path);
    free(cwd);
  }
  else
    exp_path = path;
  
#if CYGWIN_VERSION_DLL_MAJOR >= 1007
  wchar *win_wpath = cygwin_create_path(CCP_POSIX_TO_WIN_W, exp_path);
  
  // Drop long path prefix if possible,
  // because some programs have trouble with them.
  if (wcslen(win_wpath) < MAX_PATH) {
    wchar *old_win_wpath = win_wpath;
    if (wcsncmp(win_wpath, L"\\\\?\\UNC\\", 8) == 0) {
      win_wpath = wcsdup(win_wpath + 6);
      win_wpath[0] = '\\';  // Replace "\\?\UNC\" prefix with "\\"
      free(old_win_wpath);
    }
    else if (wcsncmp(win_wpath, L"\\\\?\\", 4) == 0) {
      win_wpath = wcsdup(win_wpath + 4);  // Drop "\\?\" prefix
      free(old_win_wpath);
    }
  }
#else
  char win_path[MAX_PATH];
  cygwin_conv_to_win32_path(exp_path, win_path);
  wchar *win_wpath = newn(wchar, MAX_PATH);
  MultiByteToWideChar(0, 0, win_path, -1, win_wpath, MAX_PATH);
#endif
  
  if (exp_path != path)
    free(exp_path);
  
  return win_wpath;
}
