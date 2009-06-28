#ifndef PRINT_H
#define PRINT_H

typedef struct printer_enum_tag printer_enum;
typedef struct printer_job_tag printer_job;
printer_enum *printer_start_enum(int *nprinters);
char *printer_get_name(printer_enum *, int);
void printer_finish_enum(printer_enum *);
printer_job *printer_start_job(char *printer);
void printer_job_data(printer_job *, void *, int);
void printer_finish_job(printer_job *);

#endif
