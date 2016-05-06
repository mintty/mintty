// child.c (part of mintty)
// Copyright 2008-11 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#include "child.h"

#include "term.h"
#include "charset.h"

#include "win.h"  /* win_prefix_title */

#include <pwd.h>
#include <fcntl.h>
#include <utmp.h>
#include <dirent.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/cygwin.h>

#if CYGWIN_VERSION_API_MINOR >= 93
#include <pty.h>
#else
int forkpty(int *, char *, struct termios *, struct winsize *);
#endif

#include <winbase.h>

#if CYGWIN_VERSION_DLL_MAJOR < 1007
#include <winnls.h>
#include <wincon.h>
#include <wingdi.h>
#include <winuser.h>
#endif

bool clone_size_token = true;

string child_dir = null;

static pid_t pid;
static bool killed;
static int pty_fd = -1, log_fd = -1, win_fd;

static void
childerror(char * action, bool from_fork)
{
  char * msg;
  char * err = strerror(errno);
  if (from_fork && errno == ENOENT)
    err = "There are no available terminals";
  int len = asprintf(&msg, "\033[30;%dm\033[KError: %s: %s.\033[0m\r\n", from_fork ? 41 : 43, action, err);
  if (len > 0) {
    term_write(msg, len);
    free(msg);
  }
}

static void
sigexit(int sig)
{
  if (pid)
    kill(-pid, SIGHUP);
  signal(sig, SIG_DFL);
  report_pos();
  kill(getpid(), sig);
}

void
child_create(char *argv[], struct winsize *winp)
{
  string lang = cs_lang();

  // xterm and urxvt ignore SIGHUP, so let's do the same.
  signal(SIGHUP, SIG_IGN);

  signal(SIGINT, sigexit);
  signal(SIGTERM, sigexit);
  signal(SIGQUIT, sigexit);

  // Create the child process and pseudo terminal.
  pid = forkpty(&pty_fd, 0, 0, winp);
  if (pid < 0) {
    pid = 0;
    bool rebase_prompt = (errno == EAGAIN);
    //ENOENT  There are no available terminals.
    //EAGAIN  Cannot allocate sufficient memory to allocate a task structure.
    //EAGAIN  Not possible to create a new process; RLIMIT_NPROC limit.
    //ENOMEM  Memory is tight.
    childerror("could not fork child process", true);
    if (rebase_prompt) {
      static const char msg[] =
        "\033[30;43m\033[KDLL rebasing may be required. See 'rebaseall / rebase --help'.\033[0m\r\n";
      term_write(msg, sizeof msg - 1);
    }
    term_hide_cursor();
  }
  else if (!pid) { // Child process.
#if CYGWIN_VERSION_DLL_MAJOR < 1007
    // Some native console programs require a console to be attached to the
    // process, otherwise they pop one up themselves, which is rather annoying.
    // Cygwin's exec function from 1.5 onwards automatically allocates a console
    // on an invisible window station if necessary. Unfortunately that trick no
    // longer works on Windows 7, which is why Cygwin 1.7 contains a new hack
    // for creating the invisible console.
    // On Cygwin versions before 1.5 and on Cygwin 1.5 running on Windows 7,
    // we need to create the invisible console ourselves. The hack here is not
    // as clever as Cygwin's, with the console briefly flashing up on startup,
    // but it'll do.
#if CYGWIN_VERSION_DLL_MAJOR == 1005
    DWORD win_version = GetVersion();
    win_version = ((win_version & 0xff) << 8) | ((win_version >> 8) & 0xff);
    if (win_version >= 0x0601)  // Windows 7 is NT 6.1.
#endif
      if (AllocConsole()) {
        HMODULE kernel = GetModuleHandle("kernel32");
        HWND (WINAPI *pGetConsoleWindow)(void) =
          (void *)GetProcAddress(kernel, "GetConsoleWindow");
        ShowWindowAsync(pGetConsoleWindow(), SW_HIDE);
      }
#endif

    // Reset signals
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Mimick login's behavior by disabling the job control signals
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    setenv("TERM", cfg.term, true);

    if (lang) {
      unsetenv("LC_ALL");
      unsetenv("LC_COLLATE");
      unsetenv("LC_CTYPE");
      unsetenv("LC_MONETARY");
      unsetenv("LC_NUMERIC");
      unsetenv("LC_TIME");
      unsetenv("LC_MESSAGES");
      setenv("LANG", lang, true);
    }

    // Terminal line settings
    struct termios attr;
    tcgetattr(0, &attr);
    attr.c_cc[VERASE] = cfg.backspace_sends_bs ? CTRL('H') : CDEL;
    attr.c_iflag |= IXANY | IMAXBEL;
    attr.c_lflag |= ECHOE | ECHOK | ECHOCTL | ECHOKE;
    tcsetattr(0, TCSANOW, &attr);

    // Invoke command
    execvp(cmd, argv);

    // If we get here, exec failed.
    fprintf(stderr, "\033[30;41m\033[KFailed to run %s: %s\r\n", cmd, strerror(errno));

#if CYGWIN_VERSION_DLL_MAJOR < 1005
    // Before Cygwin 1.5, the message above doesn't appear if we exit
    // immediately. So have a little nap first.
    usleep(200000);
#endif

    exit(255);
  }
  else { // Parent process.
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);

    if (cfg.utmp) {
      char *dev = ptsname(pty_fd);
      if (dev) {
        struct utmp ut;
        memset(&ut, 0, sizeof ut);

        if (!strncmp(dev, "/dev/", 5))
          dev += 5;
        strlcpy(ut.ut_line, dev, sizeof ut.ut_line);

        if (dev[1] == 't' && dev[2] == 'y')
          dev += 3;
        else if (!strncmp(dev, "pts/", 4))
          dev += 4;
        strncpy(ut.ut_id, dev, sizeof ut.ut_id);

        ut.ut_type = USER_PROCESS;
        ut.ut_pid = pid;
        ut.ut_time = time(0);
        strlcpy(ut.ut_user, getlogin() ?: "?", sizeof ut.ut_user);
        gethostname(ut.ut_host, sizeof ut.ut_host);
        login(&ut);
      }
    }
  }

  win_fd = open("/dev/windows", O_RDONLY);

  // Open log file if any
  if (*cfg.log) {
    // use cygwin conversion function to escape unencoded characters 
    // and thus avoid the locale trick (2.2.3)

    if (!wcscmp(cfg.log, L"-"))
      log_fd = fileno(stdout);
    else {
      char * log = path_win_w_to_posix(cfg.log);
      char * format = strchr(log, '%');
      if (format && * ++ format == 'd' && !strchr(format, '%')) {
        char * logf = newn(char, strlen(log) + 20);
        sprintf(logf, log, getpid());
        free(log);
        log = logf;
      }
      else if (format) {
        struct timeval now;
        gettimeofday (& now, 0);
        char * logf = newn(char, MAX_PATH + 1);
        strftime (logf, MAX_PATH, log, localtime (& now.tv_sec));
        free(log);
        log = logf;
      }

      log_fd = open(log, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (log_fd < 0) {
        // report message and filename:
        childerror("could not open log file", false);
        childerror(log, false);
      }

      free(log);
    }
  }
}

char *
child_tty(void)
{
  return ptsname(pty_fd);
}

#define patch_319

void
child_proc(void)
{
  for (;;) {
    if (term.paste_buffer)
      term_send_paste();

    struct timeval timeout = {0, 100000}, *timeout_p = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(win_fd, &fds);
    if (pty_fd >= 0)
      FD_SET(pty_fd, &fds);
#ifndef patch_319
    else
#endif
    if (pid) {
      int status;
      if (waitpid(pid, &status, WNOHANG) == pid) {
        pid = 0;

        // Decide whether we want to exit now or later
        if (killed || cfg.hold == HOLD_NEVER)
          exit_mintty();
        else if (cfg.hold == HOLD_START) {
          if (WIFSIGNALED(status) || WEXITSTATUS(status) != 255)
            exit_mintty();
        }
        else if (cfg.hold == HOLD_ERROR) {
          if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0)
              exit_mintty();
          }
          else {
            const int error_sigs =
              1<<SIGILL | 1<<SIGTRAP | 1<<SIGABRT | 1<<SIGFPE |
              1<<SIGBUS | 1<<SIGSEGV | 1<<SIGPIPE | 1<<SIGSYS;
            if (!(error_sigs & 1<<WTERMSIG(status)))
              exit_mintty();
          }
        }

        int l = 0;
        char *s = 0;
        bool err = true;
        if (WIFEXITED(status)) {
          int code = WEXITSTATUS(status);
          if (code == 0)
            err = false;
          if ((code || cfg.exit_write) && cfg.hold != HOLD_START)
            l = asprintf(&s, "%s: Exit %i", cmd, code);
        }
        else if (WIFSIGNALED(status))
          l = asprintf(&s, "%s: %s", cmd, strsignal(WTERMSIG(status)));

        if (!s && cfg.exit_write) {
          s = "TERMINATED";
          l = strlen(s);
        }
        if (s) {
          if (err)
            term_write("\033[30;41m\033[K", 11);
          else
            term_write("\033[30;42m\033[K", 11);
          term_write(s, l);
        }

        if (cfg.exit_title && *cfg.exit_title)
          win_prefix_title (cfg.exit_title);
      }
#ifdef patch_319
      if (pid != 0 && pty_fd < 0) // Pty gone, but process still there: keep checking
#else
      else // Pty gone, but process still there: keep checking
#endif
        timeout_p = &timeout;
    }

    if (select(win_fd + 1, &fds, 0, 0, timeout_p) > 0) {
      if (pty_fd >= 0 && FD_ISSET(pty_fd, &fds)) {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
        static char buf[4096];
        int len = read(pty_fd, buf, sizeof buf);
#else
        // Pty devices on old Cygwin version deliver only 4 bytes at a time,
        // so call read() repeatedly until we have a worthwhile haul.
        static char buf[512];
        uint len = 0;
        do {
          int ret = read(pty_fd, buf + len, sizeof buf - len);
          if (ret > 0)
            len += ret;
          else
            break;
        } while (len < sizeof buf);
#endif
        if (len > 0) {
          term_write(buf, len);
          if (log_fd >= 0)
            write(log_fd, buf, len);
        }
        else {
          pty_fd = -1;
          term_hide_cursor();
        }
      }
      if (FD_ISSET(win_fd, &fds))
        return;
    }
  }
}

void
child_kill(bool point_blank)
{
  if (!pid ||
      kill(-pid, point_blank ? SIGKILL : SIGHUP) < 0 ||
      point_blank)
    exit_mintty();
  killed = true;
}

bool
child_is_alive(void)
{
    return pid;
}

bool
child_is_parent(void)
{
  if (!pid)
    return false;
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
child_write(const char *buf, uint len)
{
  if (pty_fd >= 0)
    write(pty_fd, buf, len);
}

void
child_printf(const char *fmt, ...)
{
  if (pty_fd >= 0) {
    va_list va;
    va_start(va, fmt);
    char *s;
    int len = vasprintf(&s, fmt, va);
    va_end(va);
    if (len >= 0)
      write(pty_fd, s, len);
    free(s);
  }
}

void
child_send(const char *buf, uint len)
{
  term_reset_screen();
  if (term.echoing)
    term_write(buf, len);
  child_write(buf, len);
}

void
child_sendw(const wchar *ws, uint wlen)
{
  char s[wlen * cs_cur_max];
  int len = cs_wcntombn(s, ws, sizeof s, wlen);
  if (len > 0)
    child_send(s, len);
}

void
child_resize(struct winsize *winp)
{
  if (pty_fd >= 0)
    ioctl(pty_fd, TIOCSWINSZ, winp);
}

wstring
child_conv_path(wstring wpath)
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
    char *base;
    if (!*name)
      base = home;
    else {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
      // Find named user's home directory
      struct passwd *pw = getpwnam(name);
      base = (pw ? pw->pw_dir : 0) ?: "";
#else
      // Pre-1.5 Cygwin simply copies HOME into pw_dir, which is no use here.
      base = "";
#endif
    }
    exp_path = asform("%s/%s", base, rest);
  }
  else if (*path != '/') {
#if CYGWIN_VERSION_DLL_MAJOR >= 1005
    // Handle relative paths. Finding the foreground process working directory
    // requires the /proc filesystem, which isn't available before Cygwin 1.5.

    // Find pty's foreground process, if any. Fall back to child process.
    int fg_pid = (pty_fd >= 0) ? tcgetpgrp(pty_fd) : 0;
    if (fg_pid <= 0)
      fg_pid = pid;

    char *cwd = 0;
    if (fg_pid > 0) {
      char proc_cwd[32];
      sprintf(proc_cwd, "/proc/%u/cwd", fg_pid);
      cwd = realpath(proc_cwd, 0);
    }

    exp_path = asform("%s/%s", cwd ?: home, path);
    free(cwd);
#else
    // If we're lucky, the path is relative to the home directory.
    exp_path = asform("%s/%s", home, path);
#endif
  }
  else
    exp_path = path;

# if CYGWIN_VERSION_API_MINOR >= 222
  // CW_INT_SETLOCALE was introduced in API 0.222
  cygwin_internal(CW_INT_SETLOCALE);
# endif
  wchar *win_wpath = path_posix_to_win_w(exp_path);
  // Drop long path prefix if possible,
  // because some programs have trouble with them.
  if (win_wpath && wcslen(win_wpath) < MAX_PATH) {
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

  if (exp_path != path)
    free(exp_path);

  return win_wpath;
}

void
child_set_fork_dir(char * dir)
{
  strset(&child_dir, dir);
}

void
child_fork(int argc, char *argv[], int moni)
{
  pid_t clone = fork();

  if (cfg.daemonize) {
    if (clone < 0) {
      childerror("could not fork child daemon", true);
      return;  // assume next fork will fail too
    }
    if (clone > 0) {  // parent waits for intermediate child
      int status;
      waitpid(clone, &status, 0);
      return;
    }

    clone = fork();
    if (clone < 0) {
      exit(255);
    }
    if (clone > 0) {  // new parent / previous child
      exit(0);  // exit and make the grandchild a daemon
    }
  }

  if (clone == 0) {  // prepare child process to spawn new terminal
    if (pty_fd >= 0)
      close(pty_fd);
    if (log_fd >= 0)
      close(log_fd);
    close(win_fd);

    if (child_dir)
      chdir(child_dir);

#ifdef add_child_parameters
    // add child parameters
    int newparams = 0;
    char * * newargv = malloc((argc + newparams + 1) * sizeof(char *));
    int i = 0, j = 0;
    bool addnew = true;
    while (1) {
      if (addnew && (! argv[i] || strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "-") == 0)) {
        addnew = false;
        // insert additional parameters here
        newargv[j++] = "-o";
        static char parbuf1[28];  // static to prevent #530
        sprintf(parbuf1, "Rows=%d", term.rows);
        newargv[j++] = parbuf1;
        newargv[j++] = "-o";
        static char parbuf2[31];  // static to prevent #530
        sprintf(parbuf2, "Columns=%d", term.cols);
        newargv[j++] = parbuf2;
      }
      newargv[j] = argv[i];
      if (! argv[i])
        break;
      i++;
      j++;
    }
    argv = newargv;
#else
    (void) argc;
#endif

    void setenvi(char * env, int val) {
      static char valbuf[22];  // static to prevent #530
      sprintf(valbuf, "%d", val);
      setenv(env, valbuf, true);
    }

    // provide environment to clone size
    if (clone_size_token) {
      setenvi("MINTTY_ROWS", term.rows);
      setenvi("MINTTY_COLS", term.cols);
    }
    else
      clone_size_token = true;
    // provide environment to select monitor
    if (moni > 0)
      setenvi("MINTTY_MONITOR", moni);
    // propagate shortcut-inherited icon
    if (icon_is_from_shortcut)
      setenv("MINTTY_ICON", cs__wcstoutf(cfg.icon), true);

#if CYGWIN_VERSION_DLL_MAJOR >= 1005
    execv("/proc/self/exe", argv);
#else
    // /proc/self/exe isn't available before Cygwin 1.5, so use argv[0] instead.
    // Strip enclosing quotes if present.
    char *path = argv[0];
    int len = strlen(path);
    if (path[0] == '"' && path[len - 1] == '"') {
      path = strdup(path + 1);
      path[len - 2] = 0;
    }
    execvp(path, argv);
#endif
    exit(255);
  }
}
