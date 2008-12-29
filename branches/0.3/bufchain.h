#ifndef BUFCHAIN_H
#define BUFCHAIN_H

typedef struct bufchain bufchain;

bufchain *new_bufchain(void);
void bufchain_clear(bufchain *);
int bufchain_size(bufchain *);
void bufchain_add(bufchain *, const void *data, int len);
void bufchain_prefix(bufchain *, void **data, int *len);
void bufchain_consume(bufchain *, int len);
void bufchain_fetch(bufchain *, void *data, int len);

#endif
