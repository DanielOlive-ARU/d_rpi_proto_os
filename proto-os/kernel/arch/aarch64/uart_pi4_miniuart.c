#include "kernel/drivers.h"

#ifdef PLATFORM_PI4
void uart_init(void) {
  /* TODO: implement Pi 4 mini-UART init. */
}

void uart_putc(char c) {
  (void)c;
}

void uart_puts(const char *s) {
  (void)s;
}
#else
__attribute__((unused)) static const int uart_pi4_miniuart_stub = 0;
#endif
