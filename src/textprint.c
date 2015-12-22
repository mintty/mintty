#include "print.h"
#include "charset.h"
#include <sys/cygwin.h>
#include <fcntl.h>


static wstring printer = 0;
static char * pf;
static int pd;
static const wchar BOM = 0xFEFF;
static int np = 0;

void
printer_start_job(wstring printer_name)
{
  char * dirsuf = "";
  char * tmpdir = getenv("TMP");
  if (!tmpdir)
    tmpdir = getenv("TEMP");
  if (!tmpdir) {
    tmpdir = getenv("$LOCALAPPDATA");
    if (tmpdir)
      dirsuf = "/Temp";
  }
  if (!tmpdir)
    tmpdir = "/usr/tmp";

  char * user = getenv("USER");
  if (!user)
    user = getenv("USERNAME");
  if (!user)
    user = "";

  char pid[11];
  sprintf(pid, "%d", getpid());

  char n[11];
  sprintf(n, "%d", ++np);

  // compose $tmpdir/$dirsuf/mintty.print.$USER.$$
  char * pref = "mintty-print.";
  pf = malloc(strlen(tmpdir) + strlen(dirsuf) + strlen(pref) + strlen(user) + strlen(pid) + strlen(n) + 7);
  sprintf(pf, "%s%s/%s%s.%s-%s.prn", tmpdir, dirsuf, pref, user, pid, n);

  pd = open(pf, O_CREAT | O_TRUNC | O_BINARY | O_WRONLY, 0600);
  if (pd >= 0) {
    printer = printer_name;
    write(pd, &BOM, 2);
  }
}

void
printer_write(char * data, uint len)
{
  if (printer) {
    char * buf = malloc(len + 1);
    for (uint i = 0; i < len; i++) {
      if (data[i])
        buf[i] = data[i];
      else
        buf[i] = ' ';
    }
    buf[len] = '\0';
    wchar * wdata = cs__mbstowcs(buf);
    write(pd, wdata, wcslen(wdata) * sizeof(wchar));
    free(buf);
    free(wdata);
  }
}

void
printer_finish_job(void)
{
  if (printer) {
    close(pd);

    char * wf = (char *)cygwin_create_path(CCP_POSIX_TO_WIN_A, pf);
    char * pn = cs__wcstoutf(printer);

    strcpy(strchr(pf, '\0') - 4, ".cmd");
    int cmdd = open(pf, O_CREAT | O_TRUNC | O_BINARY | O_WRONLY, 0700);

    char * cmdformat = "@chcp 65001\r\n@start /min notepad /w /pt \"%s\" \"%s\"";
    char * cmd = malloc(strlen(cmdformat) - 4 + strlen(wf) + strlen(pn) + 1);
    sprintf(cmd, cmdformat, wf, pn);

    write(cmdd, cmd, strlen(cmd));
    close(cmdd);

    system(pf);

    free(cmd);
    free(wf);
    free(pn);
    free(pf);
    printer = 0;
  }
}
