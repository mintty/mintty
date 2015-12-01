#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>


static PRINTER_INFO_4 *printer_info;

uint
printer_start_enum(void)
{
  DWORD size = 0, num = 0;
  while (
    !EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                  0, 4, (LPBYTE)printer_info, size, &size, &num) &&
    GetLastError() == ERROR_INSUFFICIENT_BUFFER
  ) {
    printer_info = realloc(printer_info, size);
  }
  
  return num;
}

string
printer_get_name(uint i)
{
  return printer_info[i].pPrinterName;
}

void
printer_finish_enum(void)
{
  free(printer_info);
  printer_info = 0;
}

