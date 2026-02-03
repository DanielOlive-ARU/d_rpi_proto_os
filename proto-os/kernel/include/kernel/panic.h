#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include "kernel/types.h"

void panic(const char *msg) __attribute__((noreturn));

#endif
