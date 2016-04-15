#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>

#define dont_debug_printer


static PRINTER_INFO_4W * printer_info = null;

uint
printer_start_enum(void)
{
  DWORD size = 0, num = 0;
  while (
    !EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                  0, 4, (LPBYTE)printer_info, size, &size, &num) &&
    GetLastError() == ERROR_INSUFFICIENT_BUFFER
  ) {
    printer_info = _realloc(printer_info, size);
  }

  return num;
}

wstring
printer_get_name(uint i)
{
#ifdef debug_printer
  printf("Printer %d: %ls\n", i, printer_info[i].pPrinterName);
#endif
  return printer_info[i].pPrinterName;
}

wstring
printer_get_default(void)
{
  static wchar dp[99];
  DWORD len = sizeof dp;
  if (GetDefaultPrinterW(dp, &len))
    return dp;
  else
    return L"";
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

