#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>

#define dont_debug_printer


#define PRINFTYPE   4
static PRINTER_INFO_4W * printer_info = null;

uint
printer_start_enum(void)
{
  DWORD size = 0, num = 0;
  BOOL ok;
  ok = EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                     0, PRINFTYPE, (LPBYTE)printer_info, size, &size, &num);
  int err = GetLastError();
  //printf("printers ok %d size %d num %d err %d\n", ok, size, num, err);
  if (!ok && err == ERROR_INSUFFICIENT_BUFFER) {
    printer_info = _realloc(printer_info, size);
    ok = EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                       0, PRINFTYPE, (LPBYTE)printer_info, size, &size, &num);
    //printf("printers ok %d size %d num %d\n", ok, size, num);
  }
  return ok ? num : 0;
}

wstring
printer_get_name(uint i)
{
#ifdef debug_printer
  printf("Printer %d: %ls\n", i, printer_info[i].pPrinterName);
#endif
#if PRINFTYPE == 1
  return printer_info[i].pName;
#else
  return printer_info[i].pPrinterName;
#endif
}

wstring
printer_get_default(void)
{
  static wchar dp[99];
  DWORD len = sizeof dp;
  if (GetDefaultPrinterW(dp, &len))
    return dp;
  else
    return W("");
}

void
printer_finish_enum(void)
{
#ifdef debug_printer
  printf("Default  : %ls\n", printer_get_default());
#endif
  free(printer_info);
  printer_info = 0;
}

