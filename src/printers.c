#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
  #define LOG_ERROR(str, ...) { fprintf(stderr, "ERROR: %s:%u: " str "\n",__func__, __LINE__, ##__VA_ARGS__); }
  #define LOG_INFO(str, ...) { fprintf(stdout, "INFO: %s:%u: " str "\n", __func__, __LINE__, ##__VA_ARGS__); }
#else
  #define LOG_ERROR(str, ...)
  #define LOG_INFO(str, ...)
#endif

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
  LOG_INFO("printers ok %d size %d num %d err %d", ok, size, num, err);
  if (!ok && err == ERROR_INSUFFICIENT_BUFFER) {
    printer_info = _realloc(printer_info, size);
    ok = EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                       0, PRINFTYPE, (LPBYTE)printer_info, size, &size, &num);
    LOG_INFO("printers ok %d size %d num %d", ok, size, num);
  }
  return ok ? num : 0;
}

wstring
printer_get_name(uint i)
{
  LOG_INFO("Printer %d: %ls", i, printer_info[i].pPrinterName);
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
  LOG_INFO("Default  : %ls", printer_get_default());
  free(printer_info);
  printer_info = 0;
}

