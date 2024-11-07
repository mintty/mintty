#include "print.h"

#include <winbase.h>
#include <wingdi.h>
#include <winspool.h>

#define dont_debug_printer

#if CYGWIN_VERSION_API_MINOR < 74
#define use_enum_printers
#endif

#ifdef use_enum_printers
#define PRINFTYPE   4
static PRINTER_INFO_4W * printer_info = null;
#else
#include <w32api/winreg.h>
#include <wchar.h>
static struct printer_info {
  wchar * pPrinterName;
} * printer_info;
static int num = 0;
#endif


uint
printer_start_enum(void)
{
#ifdef use_enum_printers
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
#else
  HKEY dev;
#define PKEY "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Devices"
//#define PKEY "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\PrinterPorts"
  RegOpenKeyW(HKEY_CURRENT_USER, W(PKEY), &dev);
  DWORD num_subkeys, maxsubkeylen, maxclasslen, maxvaluelen;  // dummy
  DWORD num_values = 0, maxvalnamelen = 0;
  int res =
    RegQueryInfoKeyW(dev, 0, 0, 0, &num_subkeys, &maxsubkeylen, &maxclasslen,
                     &num_values, &maxvalnamelen, &maxvaluelen, 0, 0);
  if (res)
    return 0;
  num = num_values;
  printer_info = newn(struct printer_info, num);
  wchar valname[maxvalnamelen + 1];
  for (int i = 0; i < num; i++) {
    DWORD nambuflen = maxvalnamelen + 1;
    res = RegEnumValueW(dev, i, valname, &nambuflen, 0, 0, 0, 0);
    if (res)
      printer_info[i].pPrinterName = wcsdup(W(""));
    else
      printer_info[i].pPrinterName = wcsdup(valname);
  }
  RegCloseKey(dev);
  return num;
#endif
}

wstring
printer_get_name(uint i)
{
#ifdef debug_printer
  printf("Printer %d: %ls\n", i, printer_info[i].pPrinterName);
#endif
#if defined(use_enum_printers) && PRINFTYPE == 1
  return printer_info[i].pName;
#else
  return printer_info[i].pPrinterName;
#endif
}

wstring
printer_get_default(void)
{
#ifdef use_enum_printers
  static wchar dp[99];
  DWORD len = sizeof dp;
  if (GetDefaultPrinterW(dp, &len))
    return dp;
  else
    return W("");
#else
  HKEY win = 0;
  RegOpenKeyW(HKEY_CURRENT_USER, W("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows"), &win);
  DWORD len;
  int res = RegQueryValueExW(win, W("Device"), 0, 0, 0, &len);
  if (res)
    return 0;
  static wchar * def = 0;
  if (def)
    free(def);
  len ++;
  def = newn(wchar, len);
  res = RegQueryValueExW(win, W("Device"), 0, 0, (void *)def, &len);
  if (res)
    return 0;
  wchar * comma = wcschr(def, ',');
  if (comma)
    *comma = 0;
  return def;
#endif
}

void
printer_finish_enum(void)
{
#ifdef debug_printer
  printf("Default  : %ls\n", printer_get_default());
#endif
#ifdef use_enum_printers
#else
  for (int i = 0; i < num; i++)
    free(printer_info[i].pPrinterName);
  num = 0;
#endif
  free(printer_info);
  printer_info = 0;
}

#ifdef list_printers
// standalone test tool: list printers
//cc -include std.h -Dlist_printers printers.c -lwinspool -o printers
void
main()
{
  printf("default printer: <%ls>\n", printer_get_default());
  uint num = printer_start_enum();
  for (uint i = 0; i < num; i++)
    printf("<%ls>\n", printer_get_name(i));
  printer_finish_enum();
}
#endif
