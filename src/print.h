#ifndef PRINT_H
#define PRINT_H

// printers information
extern uint printer_start_enum(void);
extern wstring printer_get_name(uint);
extern void printer_finish_enum(void);
extern wstring printer_get_default(void);

// printer output
extern void printer_start_job(wstring printer_name);
extern void printer_write(char *, uint len);
extern void printer_wwrite(wchar *, uint len);
extern void printer_finish_job(void);

// reports printers list
extern void list_printers(void);

#endif
