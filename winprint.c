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

char *
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


static HANDLE printer;

static const DOC_INFO_1 doc_info = {
  .pDocName = "Remote printer output",
  .pOutputFile = null,
  .pDatatype = "RAW"
};

void
printer_start_job(char *printer_name)
{
  if (OpenPrinter(printer_name, &printer, 0)) {
    if (StartDocPrinter(printer, 1, (LPBYTE)&doc_info)) {
      if (StartPagePrinter(printer))
        return;
      EndDocPrinter(printer);
    }
    ClosePrinter(printer);
    printer = 0;
  }
}

void
printer_write(void *data, uint len)
{
  if (printer) {
    DWORD written;
    WritePrinter(printer, data, len, &written);
  }
}

void
printer_finish_job(void)
{
  if (printer) {
    EndPagePrinter(printer);
    EndDocPrinter(printer);
    ClosePrinter(printer);
    printer = 0;
  }
}
