#include "kernel/printk.h"
#include "kernel/drivers.h"

void printk(const char *s) {
  uart_puts(s);
}

void printk_u64(uint64_t v) {
  char buf[21];
  int i = 0;

  if (v == 0) {
    uart_putc('0');
    return;
  }

  while (v > 0 && i < (int)(sizeof(buf) - 1)) {
    buf[i++] = '0' + (char)(v % 10);
    v /= 10;
  }

  while (i > 0) {
    uart_putc(buf[--i]);
  }
}

void printk_hex_u64(uint64_t v) {
  static const char hex[] = "0123456789abcdef";
  int i;
  int started = 0;

  for (i = 15; i >= 0; i--) {
    uint8_t nibble = (uint8_t)((v >> (i * 4)) & 0xFULL);
    if (nibble != 0 || started || i == 0) {
      started = 1;
      uart_putc(hex[nibble]);
    }
  }
}
