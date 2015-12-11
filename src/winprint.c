#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>


static HANDLE printer;

static const DOC_INFO_1 doc_info = {
  .pDocName = "Mintty ANSI printer output",
  .pOutputFile = null,
  .pDatatype = "TEXT"
};

void
printer_start_job(wstring printer_name)
{
  if (OpenPrinterW((wchar *)printer_name, &printer, 0)) {
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
printer_write(char *data, uint len)
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
