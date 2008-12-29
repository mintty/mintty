#ifndef LINEDISC_H
#define LINEDISC_H

void ldisc_init(void);
void ldisc_send(const char *, int len, int interactive);

void lpage_send(int codepage, const char *, int len, int interactive);
void luni_send(const wchar *, int len, int interactive);

#endif
