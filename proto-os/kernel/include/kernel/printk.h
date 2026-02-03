#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include "kernel/types.h"

void printk(const char *s);
void printk_u64(uint64_t v);

#endif
