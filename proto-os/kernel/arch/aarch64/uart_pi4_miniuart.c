#include "kernel/arch.h"
#include "kernel/drivers.h"

/*
 * Pi 4 mini-UART (AUX UART1) minimal driver for Step 1 bring-up.
 *
 * This relies on Pi firmware having already initialised the mini-UART via
 * `enable_uart=1` in config.txt. With that flag set, firmware:
 *   - enables the AUX peripheral (AUXENB bit 0)
 *   - routes mini-UART to GPIO14 (TXD) / GPIO15 (RXD) via alt function 5
 *   - programs 8N1 at 115200 baud
 *   - pins core_freq to 250 MHz so the baud divider is stable
 *
 * We therefore only need to poll the line-status register and write bytes to
 * the data register. Once Step 1 boot is proven, this driver will be
 * expanded to own the init sequence itself (AUXENB / IER / LCR / baud / GPIO
 * alt-function programming).
 *
 * Peripheral base on BCM2711 is 0xFE000000 (low-peripheral mode, the default
 * selected by firmware in 64-bit boot).
 */

#define AUX_BASE        0xFE215000UL
#define AUX_MU_IO_REG   (AUX_BASE + 0x40)
#define AUX_MU_LSR_REG  (AUX_BASE + 0x54)

#define MU_LSR_TX_EMPTY (1u << 5)

void uart_init(void) {
  /* Firmware-initialised via config.txt enable_uart=1 — no-op here. */
}

void uart_putc(char c) {
  while ((mmio_read(AUX_MU_LSR_REG) & MU_LSR_TX_EMPTY) == 0) {
    /* spin until the TX holding register has space */
  }
  mmio_write(AUX_MU_IO_REG, (uint32_t)(uint8_t)c);
}

void uart_puts(const char *s) {
  while (*s != '\0') {
    if (*s == '\n') {
      uart_putc('\r');
    }
    uart_putc(*s);
    s++;
  }
}
