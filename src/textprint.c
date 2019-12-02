#include "print.h"
#include "charset.h"
#include "winpriv.h"  // win_prefix_title, win_unprefix_title
#include <fcntl.h>
#include <pwd.h>


static wstring printer = 0;
static char * pf;
static int pd;
static const wchar BOM = 0xFEFF;
static uint np = 0;
static struct passwd * pw;

void
printer_start_job(wstring printer_name)
{
  char * tempdir = tmpdir();

  char * user = getenv("USER");
  if (!user)
    user = getenv("USERNAME");
  if (!user) {
    pw = getpwuid(getuid());
    if (pw)
      user = pw->pw_name;
    else
      user = "";
  }

  char pid[11];
  sprintf(pid, "%d", getpid());

  char n[11];
  sprintf(n, "%d", ++np);

  // compose $tempdir/mintty.print.$USER.$$
  char * pref = "mintty-print.";
  pf = malloc(strlen(tempdir) + strlen(pref) + strlen(user) + strlen(pid) + strlen(n) + 7);
  sprintf(pf, "%s/%s%s.%s-%s.prn", tempdir, pref, user, pid, n);

  pd = open(pf, O_CREAT | O_TRUNC | O_BINARY | O_WRONLY, 0600);
  if (pd >= 0) {
    win_prefix_title(_W("[Printing...] "));

    printer = printer_name;
    write(pd, &BOM, 2);
  }
}

void
printer_wwrite(wchar * wdata, uint len)
{
  if (printer) {
    void * wbuf = wdata;
    uint wlen = len * sizeof(wchar);
    uint n;
    while ((n = write(pd, wbuf, wlen)) > 0) {
      wbuf += n;
      wlen -= n;
    }
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

    char * wf = path_posix_to_win_a(pf);

    char * pn = cs__wcstoutf(printer);

    strcpy(strchr(pf, '\0') - 4, ".cmd");
    int cmdfile = open(pf, O_CREAT | O_TRUNC | O_BINARY | O_WRONLY, 0700);

    // chcp 65001 tricks Windows into accepting UTF-8 printer and file names
    // but it needs to be restored

    // retrieve current value of `chcp`
    // which is not necessarily the same as GetOEMCP() !
    FILE * chcpcom = popen("$SYSTEMROOT/System32/chcp.com | /bin/sed -e 's,.*:,,' -e 's, ,,'", "r");
    char line[99];
    fgets(line, sizeof line, chcpcom);
    pclose(chcpcom);
    int chcp = atoi(line);

    char * cmdformat = "@%%SYSTEMROOT%%\\System32\\chcp 65001 > nul:\r\n@start /min %%SYSTEMROOT%%\\notepad /w /pt \"%s\" \"%s\"\r\n@%%SYSTEMROOT%%\\System32\\chcp %d > nul:";
    char cmd[strlen(cmdformat) - 6 + strlen(wf) + strlen(pn) + 22 + 1];
    sprintf(cmd, cmdformat, wf, pn, chcp);

    write(cmdfile, cmd, strlen(cmd));
    close(cmdfile);

    system(pf);

    free(wf);
    free(pn);
    free(pf);
    printer = 0;

    win_unprefix_title(_W("[Printing...] "));
  }
}
