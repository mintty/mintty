#ifndef PRINT_H
#define PRINT_H

uint printer_start_enum(void);
char *printer_get_name(uint);
void printer_finish_enum(void);

void printer_start_job(char *printer_name);
void printer_write(void *, uint len);
void printer_finish_job(void);

#endif
