#ifndef KERNEL_DRIVERS_H
#define KERNEL_DRIVERS_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

#endif
