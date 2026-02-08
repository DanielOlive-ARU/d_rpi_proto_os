#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/mmu.h"
#include "kernel/panic.h"

#define MMU_TABLE_ENTRIES 512UL
#define MMU_PAGE_SHIFT 12UL
#define MMU_L2_BLOCK_SHIFT 21UL
#define MMU_L2_BLOCK_SIZE (1UL << MMU_L2_BLOCK_SHIFT)

#define KERNEL_MAP_BASE KERNEL_LOAD_ADDR
#define KERNEL_MAP_SIZE 0x01000000UL

#define GIC_MAP_BASE 0x08000000UL
#define PL011_MAP_BASE 0x09000000UL

#define DESC_VALID (1UL << 0)
#define DESC_TABLE (1UL << 1)
#define DESC_AF (1UL << 10)
#define DESC_SH_INNER (3UL << 8)
#define DESC_UXN (1UL << 54)
#define DESC_PXN (1UL << 53)

#define MAIR_ATTR_NORMAL_WB_WA 0xFFUL
#define MAIR_ATTR_DEVICE_NGNRE 0x00UL

#define ATTR_IDX_NORMAL 0UL
#define ATTR_IDX_DEVICE 1UL

#define TCR_T0SZ_39BIT 25UL
#define TCR_EPD1 (1UL << 23)
#define TCR_TG0_4K (0UL << 14)
#define TCR_SH0_INNER (3UL << 12)
#define TCR_ORGN0_WB_WA (1UL << 10)
#define TCR_IRGN0_WB_WA (1UL << 8)
#define TCR_IPS_40BIT (2UL << 32)

#define SCTLR_M (1UL << 0)
#define SCTLR_C (1UL << 2)
#define SCTLR_I (1UL << 12)

extern char __stack_top;

static uint64_t g_l1_table[MMU_TABLE_ENTRIES] __attribute__((aligned(1UL << MMU_PAGE_SHIFT)));
static uint64_t g_l2_low_table[MMU_TABLE_ENTRIES] __attribute__((aligned(1UL << MMU_PAGE_SHIFT)));
static uint64_t g_l2_kernel_table[MMU_TABLE_ENTRIES] __attribute__((aligned(1UL << MMU_PAGE_SHIFT)));

static inline uint64_t l1_index(uint64_t va) {
  return (va >> 30) & 0x1FFUL;
}

static inline uint64_t l2_index(uint64_t va) {
  return (va >> MMU_L2_BLOCK_SHIFT) & 0x1FFUL;
}

static inline uint64_t table_desc(uint64_t pa) {
  return (pa & ~((1UL << MMU_PAGE_SHIFT) - 1UL)) | DESC_VALID | DESC_TABLE;
}

static inline uint64_t block_desc_normal(uint64_t pa) {
  return (pa & ~(MMU_L2_BLOCK_SIZE - 1UL)) | DESC_VALID | DESC_AF | DESC_SH_INNER |
         (ATTR_IDX_NORMAL << 2);
}

static inline uint64_t block_desc_device(uint64_t pa) {
  return (pa & ~(MMU_L2_BLOCK_SIZE - 1UL)) | DESC_VALID | DESC_AF |
         (ATTR_IDX_DEVICE << 2) | DESC_UXN | DESC_PXN;
}

static void clear_tables(void) {
  uint64_t i;
  for (i = 0; i < MMU_TABLE_ENTRIES; i++) {
    g_l1_table[i] = 0;
    g_l2_low_table[i] = 0;
    g_l2_kernel_table[i] = 0;
  }
}

static void map_2m_block(uint64_t *l2_table, uint64_t va, uint64_t pa, int is_device) {
  uint64_t idx = l2_index(va);
  l2_table[idx] = is_device ? block_desc_device(pa) : block_desc_normal(pa);
}

static void build_identity_map(void) {
  uint64_t offset;

  g_l1_table[l1_index(GIC_MAP_BASE)] = table_desc((uint64_t)(uintptr_t)g_l2_low_table);
  g_l1_table[l1_index(KERNEL_MAP_BASE)] = table_desc((uint64_t)(uintptr_t)g_l2_kernel_table);

  map_2m_block(g_l2_low_table, GIC_MAP_BASE, GIC_MAP_BASE, 1);
  map_2m_block(g_l2_low_table, PL011_MAP_BASE, PL011_MAP_BASE, 1);

  for (offset = 0; offset < KERNEL_MAP_SIZE; offset += MMU_L2_BLOCK_SIZE) {
    map_2m_block(g_l2_kernel_table,
                 KERNEL_MAP_BASE + offset,
                 KERNEL_MAP_BASE + offset,
                 0);
  }
}

static void validate_kernel_window(void) {
  uintptr_t kernel_limit = KERNEL_MAP_BASE + KERNEL_MAP_SIZE;
  uintptr_t stack_top = (uintptr_t)&__stack_top;

  if (stack_top >= kernel_limit) {
    panic("MMU kernel map window too small");
  }

  if ((uintptr_t)g_l1_table >= kernel_limit || (uintptr_t)g_l2_low_table >= kernel_limit ||
      (uintptr_t)g_l2_kernel_table >= kernel_limit) {
    panic("MMU page tables outside kernel map");
  }
}

void mmu_init(void) {
  uint64_t mair;
  uint64_t tcr;
  uint64_t sctlr;

  asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  if (sctlr & SCTLR_M) {
    return;
  }

  validate_kernel_window();
  clear_tables();
  build_identity_map();

  /* Normal memory uses MAIR WB/WA; device attribute remains unchanged. */
  mair = (MAIR_ATTR_NORMAL_WB_WA << (ATTR_IDX_NORMAL * 8)) |
         (MAIR_ATTR_DEVICE_NGNRE << (ATTR_IDX_DEVICE * 8));
  /* IRGN0/ORGN0 are WB/WA to match cacheable normal-memory attributes. */
  tcr = TCR_T0SZ_39BIT | TCR_EPD1 | TCR_TG0_4K | TCR_SH0_INNER |
        TCR_ORGN0_WB_WA | TCR_IRGN0_WB_WA |
        TCR_IPS_40BIT;

  asm volatile("msr mair_el1, %0" ::"r"(mair));
  asm volatile("msr ttbr0_el1, %0" ::"r"((uint64_t)(uintptr_t)g_l1_table));
  asm volatile("msr ttbr1_el1, xzr");
  asm volatile("msr tcr_el1, %0" ::"r"(tcr));

  dsb_sy();
  isb();
  asm volatile("tlbi vmalle1");
  dsb_sy();
  isb();

  asm volatile("ic iallu");
  dsb_sy();
  isb();

  sctlr |= (SCTLR_M | SCTLR_C | SCTLR_I);
  asm volatile("msr sctlr_el1, %0" ::"r"(sctlr));
  isb();
}
