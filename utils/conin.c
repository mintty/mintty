// conin.c
// Copyright 2009 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#define WINVER 0x501
#define _WIN32_WINNT WINVER
#define _WIN32_IE WINVER

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <wait.h>
#include <termios.h>
#include <windows.h>
#include <sys/cygwin.h>
#include <readline/readline.h>
#include <readline/history.h>

static int pid;
static HANDLE conin;

struct termios orig_tattr, raw_tattr, readline_tattr;

static char prompt[256];
static int prompt_len;

static bool in_readline_mode = false;

static void
sigchld(int sig)
{
  int status;
  if (wait(&status) != pid)
    return;
  tcsetattr (0, TCSANOW, &orig_tattr);
  if (WIFEXITED(status))
    exit(WEXITSTATUS(status));
  else if (WIFSIGNALED(status)) {
    signal(SIGINT,  SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
    signal(SIGWINCH,SIG_DFL);
    raise(WTERMSIG(status));
  }
}

static void
sigfwd(int sig)
{
  kill(pid, sig);
}

static void
error(char *msg)
{
  fputs(msg, stderr);
  exit(1);
}

static void
sigact(int sig, void (*handler)(int), int flags)
{
  struct sigaction action;
  action.sa_handler = handler;
  action.sa_mask = 0;
  action.sa_flags = flags;
  sigaction(sig, &action, 0);
}

static void
forward_output(int src_fd, int dest_fd)
{
  char buf[256];
  int buf_len = read(src_fd, buf, sizeof buf);
  write(dest_fd, buf, buf_len);

  // Look for a line feed
  int p;
  for (p = buf_len; p; p--) {
    if (buf[p-1] == '\n') {
      prompt_len = buf_len - p;
      memcpy(prompt, buf + p, prompt_len);
      break;
    }
  }

  if (!p) {
    int old_prompt_len = prompt_len;
    prompt_len += buf_len;
    if (prompt_len < sizeof prompt)
      memcpy(prompt + old_prompt_len, buf, buf_len);
  }
  
  prompt[prompt_len < sizeof prompt ? prompt_len : 0] = 0;
}

static void
trans_char(INPUT_RECORD *inrecs, char c)
{
  SHORT vkks = VkKeyScan(c);
  UCHAR vk = vkks;
  bool shift = vkks & 0x100, ctrl = vkks & 0x200, alt = vkks & 0x400;
  WORD vsc = MapVirtualKey(vk, 0 /* MAPVK_VK_TO_VSC */);
  inrecs[0] = inrecs[1] = (INPUT_RECORD){
    .EventType = KEY_EVENT,
    .Event = {
      .KeyEvent = {
        .bKeyDown = false,
        .wRepeatCount = 1,
        .wVirtualKeyCode = vk,
        .wVirtualScanCode = vsc,
        .uChar = { .AsciiChar = c },
        .dwControlKeyState =
          shift * SHIFT_PRESSED |
          ctrl  * LEFT_CTRL_PRESSED |
          alt   * RIGHT_ALT_PRESSED
      }
    }
  };
  inrecs[0].Event.KeyEvent.bKeyDown = true;
}

static void
forward_raw_char(void)
{
  static enum {START, SEEN_ESC, SEEN_CSI} state = START;

  char c = getchar();
  UCHAR vk;
  bool shift = 0, ctrl = 0, alt = 0;
  switch (state) {
    case START:
      if (c == '\e') {
        state = SEEN_ESC;
        return;
      }
      if (c == '\n')
        c = '\r';
      else if (c == 0x7F)
        c = '\b';
      
      
      SHORT vkks = VkKeyScan(c);
      vk = vkks;
      shift = vkks & 0x100, ctrl = vkks & 0x200, alt = vkks & 0x400;
      break;
    case SEEN_ESC:
      state = c == '[' ? SEEN_CSI : START;
      return;
    case SEEN_CSI:
      state = START;
      switch (c) {
        case 'A': vk = VK_UP; break;
        case 'B': vk = VK_DOWN; break;
        case 'C': vk = VK_RIGHT; break;
        case 'D': vk = VK_LEFT; break;
        case 'F': vk = VK_END; break;
        case 'H': vk = VK_HOME; break;
        default: return;
      }
      c = 0;
      break;
  }
  
  WORD vsc = MapVirtualKey(vk, 0 /* MAPVK_VK_TO_VSC */);
  INPUT_RECORD inrec = {
    .EventType = KEY_EVENT,
    .Event = {
      .KeyEvent = {
        .bKeyDown = true,
        .wRepeatCount = 1,
        .wVirtualKeyCode = vk,
        .wVirtualScanCode = vsc,
        .uChar = { .AsciiChar = c },
        .dwControlKeyState =
          shift * SHIFT_PRESSED |
          ctrl  * LEFT_CTRL_PRESSED |
          alt   * RIGHT_ALT_PRESSED
      }
    }
  };
  
  DWORD written;
  WriteConsoleInput(conin, &inrec, 1, &written);
  inrec.Event.KeyEvent.bKeyDown = false;
  WriteConsoleInput(conin, &inrec, 1, &written);
}

static void
rl_callback(char *line)
{
  rl_callback_handler_remove();
  in_readline_mode = false;
  tcsetattr (0, TCSANOW, &raw_tattr);

  if (!line) {
    INPUT_RECORD inrecs[2];
    trans_char(inrecs, 26); // ^Z
    WriteConsoleInput(conin, inrecs, 2, &(DWORD){0});
  }
  else {
    if (*line)
      add_history(line);
    size_t len = strlen(line) + 1;
    line[len - 1] = '\r';
    INPUT_RECORD inrecs[len * 2];
    for (int i = 0; i < len; i++)
      trans_char(inrecs + i*2, line[i]);
    WriteConsoleInput(conin, inrecs, len * 2, &(DWORD){0});
  }
}

static void
forward_input(void)
{  
  if (!in_readline_mode) {
    DWORD mode;
    GetConsoleMode(conin, &mode);
    if ((mode & 7) == 7) { // PROCESSED_INPUT, LINE_INPUT, ECHO_INPUT
      tcsetattr (0, TCSANOW, &readline_tattr);
      rl_already_prompted = true;
      rl_callback_handler_install(prompt, rl_callback);
      in_readline_mode = true;
    }
  }
  if (in_readline_mode)
    rl_callback_read_char();
  else
    forward_raw_char();
}

int
main(int argc, char *argv[])
{
  if (argc < 2)
    return 2;
  
  int cmdout_pipe[2], cmderr_pipe[2];
  if (pipe(cmdout_pipe) != 0 || pipe(cmderr_pipe) != 0)
    error("Could not create pipes");
  
  spawnl(_P_WAIT, "/bin/test", "/bin/test", 0);
  
  pid = fork();
  if (pid < 0)
    error("Could not create child process");
  else if (pid == 0) {
    setsid();
    AttachConsole(-1);
    
    // Child process
    close(0);
    if (open("/dev/conin", O_RDONLY) != 0)
      error("Could not open /dev/conin");
    
    dup2(cmdout_pipe[1], 1);
    dup2(cmderr_pipe[1], 2);
    close(cmdout_pipe[0]);
    close(cmderr_pipe[0]);
    
    execvp(argv[1], argv + 1);
    
    error("Could not execute command");
  }
  
  sigact(SIGCHLD, sigchld,  SA_NOCLDSTOP);
  signal(SIGINT,  sigfwd);
  signal(SIGHUP,  sigfwd);
  signal(SIGQUIT, sigfwd);
  signal(SIGABRT, sigfwd);
  signal(SIGTERM, sigfwd);
  signal(SIGUSR1, sigfwd);
  signal(SIGUSR2, sigfwd);
  signal(SIGWINCH,sigfwd);

  // Get hold of the console input buffer
  conin =
    CreateFile(
      "CONIN$",
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
      &(SECURITY_ATTRIBUTES){
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = 0,
        .bInheritHandle = true
      },
      OPEN_EXISTING, 0, 0
    );
  if (conin == INVALID_HANDLE_VALUE)
    error("Could not open console input buffer");

  close(cmdout_pipe[1]);
  close(cmderr_pipe[1]);
  int cmdout_fd = cmdout_pipe[0], cmderr_fd = cmderr_pipe[0];
  
  tcgetattr (0, &orig_tattr);

  readline_tattr = orig_tattr;
  readline_tattr.c_lflag &= ~ISIG;
  
  raw_tattr = orig_tattr;
  raw_tattr.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
  tcsetattr (0, TCSANOW, &raw_tattr);
  
  for (;;) {
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(0, &fdset);
    FD_SET(cmdout_fd, &fdset);
    FD_SET(cmderr_fd, &fdset);

    if (select(cmderr_fd + 1, &fdset, 0, 0, 0) < 0)
      break;
    
    if (FD_ISSET(cmderr_fd, &fdset))
      forward_output(cmderr_fd, 2);

    if (FD_ISSET(cmdout_fd, &fdset))
      forward_output(cmdout_fd, 1);

    if (FD_ISSET(0, &fdset))
      forward_input();
  }
  
  int status;
  while (wait(&status) != pid);

  return 0;
}
