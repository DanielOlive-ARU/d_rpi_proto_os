#include "kernel/arch.h"

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

#define GICD_CTLR       (GICD_BASE + 0x000)
#define GICD_ISENABLER0 (GICD_BASE + 0x100)
#define GICD_ICENABLER0 (GICD_BASE + 0x180)
#define GICD_IPRIORITYR (GICD_BASE + 0x400)

#define GICC_CTLR (GICC_BASE + 0x000)
#define GICC_PMR  (GICC_BASE + 0x004)
#define GICC_IAR  (GICC_BASE + 0x00C)
#define GICC_EOIR (GICC_BASE + 0x010)

#define TIMER_IRQ_ID 27u

void gic_init(void) {
  mmio_write(GICD_CTLR, 0);
  mmio_write(GICC_CTLR, 0);

  uint32_t reg = GICD_IPRIORITYR + (TIMER_IRQ_ID / 4) * 4;
  uint32_t shift = (TIMER_IRQ_ID % 4) * 8;
  uint32_t val = mmio_read(reg);
  val &= ~(0xFFu << shift);
  val |= (0x80u << shift);
  mmio_write(reg, val);

  mmio_write(GICD_ICENABLER0, (1u << TIMER_IRQ_ID));
  mmio_write(GICD_ISENABLER0, (1u << TIMER_IRQ_ID));

  mmio_write(GICC_PMR, 0xFF);
  mmio_write(GICC_CTLR, 1);
  mmio_write(GICD_CTLR, 1);
}

void gic_handle_irq(void) {
  uint32_t iar = mmio_read(GICC_IAR);
  uint32_t id = iar & 0x3FFu;

  if (id == TIMER_IRQ_ID) {
    timer_handle_irq();
  }

  mmio_write(GICC_EOIR, iar);
}
