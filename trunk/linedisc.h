#ifndef LINEDISC_H
#define LINEDISC_H

void ldisc_init(void);
void ldisc_flush(void);
void ldisc_send(const char *, int len, bool interactive);
void luni_send(const wchar *, int len, bool interactive);

#endif
