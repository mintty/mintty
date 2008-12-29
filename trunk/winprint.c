#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>

struct printer_enum_tag {
  int nprinters;
  LPPRINTER_INFO_4 info;
};

struct printer_job_tag {
  HANDLE hprinter;
};

static BYTE *
printer_add_enum(int param, DWORD level, BYTE * buffer, int offset,
                 int *nprinters_ptr)
{
  DWORD needed, nprinters;

  buffer = renewn(buffer, offset + 512);

 /*
  * Exploratory call to EnumPrinters to determine how much space
  * we'll need for the output. Discard the return value since it
  * will almost certainly be a failure due to lack of space.
  */
  EnumPrinters(param, null, level, buffer + offset, 512, &needed, &nprinters);

  if (needed < 512)
    needed = 512;

  buffer = renewn(buffer, offset + needed);

  if (EnumPrinters
      (param, null, level, buffer + offset, needed, &needed, &nprinters) == 0)
    return null;

  *nprinters_ptr += nprinters;

  return buffer;
}

printer_enum *
printer_start_enum(int *nprinters_ptr)
{
  printer_enum *ret = new(printer_enum);
  BYTE *buffer = null, *retval;

  *nprinters_ptr = 0;   /* default return value */
  buffer = newn(BYTE, 512);

  retval =
    printer_add_enum(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                     4, buffer, 0, nprinters_ptr);
  if (!retval)
    goto error;
  else
    buffer = retval;

  ret->info = (LPPRINTER_INFO_4) buffer;
  ret->nprinters = *nprinters_ptr;

  return ret;

 error:
  free(buffer);
  free(ret);
  *nprinters_ptr = 0;
  return null;
}

char *
printer_get_name(printer_enum * pe, int i)
{
  if (!pe)
    return null;
  if (i < 0 || i >= pe->nprinters)
    return null;
  return pe->info[i].pPrinterName;
}

void
printer_finish_enum(printer_enum * pe)
{
  if (!pe)
    return;
  free(pe->info);
  free(pe);
}

printer_job *
printer_start_job(char *printer)
{
  printer_job *ret = new(printer_job);
  DOC_INFO_1 docinfo;
  int jobstarted = 0, pagestarted = 0;

  ret->hprinter = null;
  if (!OpenPrinter(printer, &ret->hprinter, null))
    goto error;

  docinfo.pDocName = "PuTTY remote printer output";
  docinfo.pOutputFile = null;
  docinfo.pDatatype = "RAW";

  if (!StartDocPrinter(ret->hprinter, 1, (LPBYTE) & docinfo))
    goto error;
  jobstarted = 1;

  if (!StartPagePrinter(ret->hprinter))
    goto error;
  pagestarted = 1;

  return ret;

 error:
  if (pagestarted)
    EndPagePrinter(ret->hprinter);
  if (jobstarted)
    EndDocPrinter(ret->hprinter);
  if (ret->hprinter)
    ClosePrinter(ret->hprinter);
  free(ret);
  return null;
}

void
printer_job_data(printer_job * pj, void *data, int len)
{
  DWORD written;

  if (!pj)
    return;

  WritePrinter(pj->hprinter, data, len, &written);
}

void
printer_finish_job(printer_job * pj)
{
  if (!pj)
    return;

  EndPagePrinter(pj->hprinter);
  EndDocPrinter(pj->hprinter);
  ClosePrinter(pj->hprinter);
  free(pj);
}
