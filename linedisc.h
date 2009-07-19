#ifndef LINEDISC_H
#define LINEDISC_H

void ldisc_init(void);
void ldisc_send(const char *, int len, bool interactive);
void ldisc_printf(bool interactive, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
void luni_send(const wchar *, int len, bool interactive);

#endif
