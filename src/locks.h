#ifndef LOCK_H
#define LOCK_H

#include <stdint.h>

int lock_read(uint64_t entry_off);
int lock_write(uint64_t entry_off);
void unlock(int fd);
int lock_super(void);
void unlock_super(int fd);

#endif