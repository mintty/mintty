#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>

struct termios saved_tattr;

void 
restore_tattr (void) 
{
  if (isatty (STDIN_FILENO))
    tcsetattr (STDIN_FILENO, TCSANOW, &saved_tattr);
}

int 
main (void) 
{
  if (isatty (STDIN_FILENO)) {
    tcgetattr (STDIN_FILENO, &saved_tattr);    
    struct termios tattr = saved_tattr;
    tattr.c_iflag &= 
      ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tattr.c_oflag &= ~OPOST;
    tattr.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    tattr.c_cflag &= ~(CSIZE|PARENB);
    tattr.c_cflag |= CS8;  
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
    atexit (restore_tattr);
  }
  setbuf(stdout, 0);
  int c;
  while ((c = getchar()) && c != -1) {
    switch (c) {
      case 0x00 ... 0x1f:  putchar('^'); putchar(c + 0x40); break;
      case '^': case '\\': putchar('\\'); putchar(c); break;
      case 0x7f: putchar('^'); putchar('?'); break;
      default: 
        if (c < 0x80) putchar(c); else printf("\0x%X",c);
        break;
    }
  }
  putchar ('\n');
  return 0;
}
