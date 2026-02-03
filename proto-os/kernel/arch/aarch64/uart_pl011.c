#include "kernel/arch.h"
#include "kernel/drivers.h"

#define UART_BASE 0x09000000UL
#define UART_DR   (UART_BASE + 0x00)
#define UART_FR   (UART_BASE + 0x18)
#define UART_IBRD (UART_BASE + 0x24)
#define UART_FBRD (UART_BASE + 0x28)
#define UART_LCRH (UART_BASE + 0x2C)
#define UART_CR   (UART_BASE + 0x30)
#define UART_IMSC (UART_BASE + 0x38)
#define UART_ICR  (UART_BASE + 0x44)

void uart_init(void) {
  mmio_write(UART_CR, 0);
  mmio_write(UART_ICR, 0x7FF);

  mmio_write(UART_IBRD, 13);
  mmio_write(UART_FBRD, 1);
  mmio_write(UART_LCRH, (3 << 5));

  mmio_write(UART_IMSC, 0);
  mmio_write(UART_CR, (1 << 9) | (1 << 8) | 1);
}

void uart_putc(char c) {
  if (c == '\n') {
    uart_putc('\r');
  }
  while (mmio_read(UART_FR) & (1 << 5)) {
  }
  mmio_write(UART_DR, (uint32_t)c);
}

void uart_puts(const char *s) {
  while (*s) {
    uart_putc(*s++);
  }
}
