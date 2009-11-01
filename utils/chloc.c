#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <locale.h>

static const char *
getlocenv(const char *name)
{
  const char *val = getenv(name);
  return val && *val ? val : 0;
}

int
main(int argc, const char *argv[])
{
  const char *locale =
    getlocenv("LC_ALL") ?: 
    getlocenv("LC_CTYPE") ?: 
    getlocenv("LANG") ?: 
    setlocale(LC_CTYPE, "") ?:
    setlocale(LC_CTYPE, 0);
  
  puts(locale);
  
  printf("\e]7776;%s\a", locale);
  fflush(stdout);
  
  if (argc <= 1)
    return 0;
  
  spawnvp(_P_WAIT, argv[1], argv + 1);
  
  printf("\e]7776;\a");
  fflush(stdout);
}
