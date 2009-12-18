// conin.c
// Copyright 2009 Andy Koppe
// Licensed under the terms of the GNU General Public License v3 or later.

#define WINVER 0x501
#define _WIN32_WINNT WINVER
#define _WIN32_IE WINVER

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <process.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <wait.h>
#include <termios.h>
#include <locale.h>
#include <langinfo.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <windows.h>

static int pid;
static HANDLE conin;

struct termios orig_tattr, raw_tattr;

static char prompt[256];
static int prompt_len;

static bool in_readline_mode = false;

static const struct {
  ushort cp;
  const char *name;
}
cs_names[] = {
  {CP_UTF8, "UTF-8"},
  {  20127, "ASCII"},
  {  20866, "KOI8-R"},
  {  21866, "KOI8-U"},
  {    936, "GBK"},
  {    950, "BIG5"},
  {    932, "SJIS"},
  {  20932, "EUCJP"},
  {    949, "EUCKR"},
};

static int
cs_cp(const char *name)
{
  uint iso;
  if (sscanf(name, "ISO-8859-%u", &iso) == 1)
    return 28590 + iso;
  uint cp;
  if (sscanf(name, "CP%u", &cp) == 1)
    return cp;
  for (uint i = 0; i < sizeof(cs_names) / sizeof(*cs_names); i++) {
    if (strcmp(name, cs_names[i].name) == 0)
      return cs_names[i].cp;
  }
  return 0;
}

static void
sigchld(int sig)
{
  int status;
  if (wait(&status) != pid)
    return;
  tcsetattr(0, TCSANOW, &orig_tattr);
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
trans_char(INPUT_RECORD **ppinrec, char c)
{
  wchar_t wc;
  switch (mbrtowc(&wc, &c, 1, 0)) {
    case -2: return;
    case -1:
      mbrtowc(0, 0, 0, 0);
      return;
  }
  
  SHORT vkks = VkKeyScan(c);
  UCHAR vk = vkks;
  bool shift = vkks & 0x100, ctrl = vkks & 0x200, alt = vkks & 0x400;
  WORD vsc = MapVirtualKey(vk, 0 /* MAPVK_VK_TO_VSC */);
  (*ppinrec)[0] = (*ppinrec)[1] = (INPUT_RECORD){
    .EventType = KEY_EVENT,
    .Event = {
      .KeyEvent = {
        .bKeyDown = false,
        .wRepeatCount = 1,
        .wVirtualKeyCode = vk,
        .wVirtualScanCode = vsc,
        .uChar = { .UnicodeChar = wc },
        .dwControlKeyState =
          shift * SHIFT_PRESSED |
          ctrl  * LEFT_CTRL_PRESSED |
          alt   * RIGHT_ALT_PRESSED
      }
    }
  };
  (*ppinrec)[0].Event.KeyEvent.bKeyDown = true;
  *ppinrec += 2;
}

static void
rl_callback(char *line)
{
  in_readline_mode = false;
  rl_callback_handler_remove();

  if (!line) {
    INPUT_RECORD inrecs[2], *pinrec = inrecs;
    trans_char(&pinrec, 26); // ^Z
    WriteConsoleInputW(conin, inrecs, pinrec - inrecs, &(DWORD){0});
  }
  else {
    if (*line)
      add_history(line);
    size_t len = strlen(line) + 1;
    line[len - 1] = '\r';
    INPUT_RECORD inrecs[len * 2], *pinrec = inrecs;
    for (int i = 0; i < len; i++)
      trans_char(&pinrec, line[i]);
    WriteConsoleInputW(conin, inrecs, pinrec - inrecs, &(DWORD){0});
  }

  // Fresh prompt.
  prompt_len = 0;
  *prompt = 0;
}

static void
forward_raw_char(void)
{
  static enum {START, SEEN_ESC, SEEN_CSI} state = START;
  char c = getchar();
  INPUT_RECORD inrecs[2];
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
      INPUT_RECORD *pinrec = inrecs;
      trans_char(&pinrec, c);
      WriteConsoleInputW(conin, inrecs, pinrec - inrecs, &(DWORD){0});
      return;
    case SEEN_ESC:
      state = c == '[' ? SEEN_CSI : START;
      return;
    case SEEN_CSI:
      state = START;
      UCHAR vk;
      switch (c) {
        case 'A': vk = VK_UP; break;
        case 'B': vk = VK_DOWN; break;
        case 'C': vk = VK_RIGHT; break;
        case 'D': vk = VK_LEFT; break;
        case 'F': vk = VK_END; break;
        case 'H': vk = VK_HOME; break;
        default: return;
      }
      WORD vsc = MapVirtualKey(vk, 0 /* MAPVK_VK_TO_VSC */);
      inrecs[0] = inrecs[1] = (INPUT_RECORD){
        .EventType = KEY_EVENT,
        .Event = {
          .KeyEvent = {
            .bKeyDown = false,
            .wRepeatCount = 1,
            .wVirtualKeyCode = vk,
            .wVirtualScanCode = vsc,
            .uChar = { .UnicodeChar = 0 },
            .dwControlKeyState = 0
          }
        }
      };
      inrecs[0].Event.KeyEvent.bKeyDown = true;
      WriteConsoleInputW(conin, inrecs, 2, &(DWORD){0});
      break;
  }
}

static void
forward_input(void)
{  
  if (!in_readline_mode) {
    DWORD mode;
    GetConsoleMode(conin, &mode);
    if ((mode & 7) == 7) { // PROCESSED_INPUT, LINE_INPUT, ECHO_INPUT
      in_readline_mode = true;
      tcsetattr(0, TCSANOW, &orig_tattr);
      rl_already_prompted = true;
      rl_callback_handler_install(prompt, rl_callback);
    }
  }
  if (in_readline_mode) {
    rl_callback_read_char();
    if (!in_readline_mode)
      tcsetattr(0, TCSANOW, &raw_tattr);
  }
  else
    forward_raw_char();
}

int
main(int argc, char *argv[])
{
  if (argc < 2)
    return 2;
  
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0)
    error("Could not create output pipe");
  
  // This hack prompts Cygwin to allocate an invisible console for us.
  spawnl(_P_WAIT, "/bin/test", "/bin/test", 0);
  
  // Get hold of the console input buffer
  conin =
    CreateFile(
      "CONIN$",
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
      0, OPEN_EXISTING, 0, 0
    );
  if (conin == INVALID_HANDLE_VALUE)
    error("Could not open console input buffer");

  // Configure charsets
  setlocale(LC_CTYPE, "");
  int cp = cs_cp(nl_langinfo(CODESET));
  SetConsoleCP(cp);
  SetConsoleOutputCP(cp);

  pid = fork();
  if (pid < 0)
    error("Could not create child process");
  else if (pid == 0) {
    setsid();
    AttachConsole(-1);
    
    close(0);
    if (open("/dev/conin", O_RDONLY) != 0)
      error("Could not open /dev/conin");
    
    close(pipe_fds[0]);
    int pipe_fd = pipe_fds[1];
    dup2(pipe_fd, 1);
    dup2(pipe_fd, 2);
    if (pipe_fd > 2)
      close(pipe_fd);
    
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
  
  close(pipe_fds[1]);
  int pipe_fd = pipe_fds[0];
  
  tcgetattr (0, &orig_tattr);
  raw_tattr = orig_tattr;
  raw_tattr.c_lflag &= ~(ECHO|ICANON|IEXTEN);
  tcsetattr(0, TCSANOW, &raw_tattr);
  
  rl_catch_signals = false;
  rl_bind_key ('\t', rl_insert);
  
  for (;;) {
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(0, &fdset);
    FD_SET(pipe_fd, &fdset);
    
    if (select(pipe_fd + 1, &fdset, 0, 0, 0) > 0) {
      if (FD_ISSET(pipe_fd, &fdset))
        forward_output(pipe_fd, 1);
      if (FD_ISSET(0, &fdset))
        forward_input();
    }
  }
}
