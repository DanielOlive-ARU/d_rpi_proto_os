CROSS ?= aarch64-linux-gnu-

CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_IMG := $(BUILD_DIR)/kernel8.img
CONFIG_STAMP := $(BUILD_DIR)/.build_config

KERNEL_FLAVOR ?= MONO
BENCH_MODE ?= OFF
PLATFORM ?= QEMU_VIRT
SMOKE ?= 0
DEBUG_TICK ?= 1
DEBUG_EARLY ?= 0

CFLAGS := -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -O2 -g
CFLAGS += -mgeneral-regs-only
CFLAGS += -I kernel/include
CFLAGS += -DDEBUG_TICK=$(DEBUG_TICK) -DDEBUG_EARLY=$(DEBUG_EARLY)

ifeq ($(KERNEL_FLAVOR),MONO)
CFLAGS += -DKERNEL_FLAVOR_MONO
else ifeq ($(KERNEL_FLAVOR),MICRO)
CFLAGS += -DKERNEL_FLAVOR_MICRO
else
$(error Unsupported KERNEL_FLAVOR=$(KERNEL_FLAVOR))
endif

ifeq ($(BENCH_MODE),OFF)
CFLAGS += -DBENCH_MODE_OFF
else ifeq ($(BENCH_MODE),LATENCY)
CFLAGS += -DBENCH_MODE_LATENCY
else ifeq ($(BENCH_MODE),RECOVERY)
CFLAGS += -DBENCH_MODE_RECOVERY
else
$(error Unsupported BENCH_MODE=$(BENCH_MODE))
endif

ifeq ($(PLATFORM),QEMU_VIRT)
CFLAGS += -mcpu=cortex-a57
PLATFORM_EXCLUDE_C := kernel/arch/aarch64/uart_pi4_miniuart.c
else ifeq ($(PLATFORM),PI4)
CFLAGS += -mcpu=cortex-a72 -DPLATFORM_PI4
PLATFORM_EXCLUDE_C := kernel/arch/aarch64/uart_pl011.c
else
$(error Unsupported PLATFORM=$(PLATFORM))
endif

ifeq ($(SMOKE),1)
ifneq ($(PLATFORM),PI4)
$(error SMOKE=1 is only valid with PLATFORM=PI4)
endif
CFLAGS += -DPI4_SMOKE
else ifeq ($(SMOKE),0)
# default path — no smoke short-circuit
else
$(error Unsupported SMOKE=$(SMOKE))
endif

ASFLAGS := $(CFLAGS)
LDFLAGS := -T kernel/arch/aarch64/linker.ld

SRCS_C := $(filter-out $(PLATFORM_EXCLUDE_C),$(shell find kernel -name '*.c'))
SRCS_S := $(shell find kernel -name '*.S')
OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/%.o,$(SRCS_C)) \
        $(patsubst kernel/%.S,$(BUILD_DIR)/%.o,$(SRCS_S))

.PHONY: build clean mono-qemu micro-qemu bench-mono-qemu bench-micro-qemu recovery-micro-qemu pi4-smoke-build config-check

build: config-check $(KERNEL_ELF) $(KERNEL_IMG)

config-check:
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(CONFIG_STAMP) ] || [ "$$(cat $(CONFIG_STAMP))" != "$(KERNEL_FLAVOR):$(BENCH_MODE):$(PLATFORM):$(SMOKE)" ]; then \
		rm -rf $(BUILD_DIR); \
		mkdir -p $(BUILD_DIR); \
		printf "%s\n" "$(KERNEL_FLAVOR):$(BENCH_MODE):$(PLATFORM):$(SMOKE)" > $(CONFIG_STAMP); \
	fi

$(CONFIG_STAMP):
	@mkdir -p $(BUILD_DIR)
	@printf "%s\n" "$(KERNEL_FLAVOR):$(BENCH_MODE):$(PLATFORM):$(SMOKE)" > $@

$(BUILD_DIR)/el0/user_tasks.o: CFLAGS += -ffixed-x30

$(BUILD_DIR)/%.o: kernel/%.c $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: kernel/%.S $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) kernel/arch/aarch64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

mono-qemu:
	$(MAKE) KERNEL_FLAVOR=MONO BENCH_MODE=OFF PLATFORM=QEMU_VIRT SMOKE=0 build
	./scripts/run_qemu.sh

micro-qemu:
	$(MAKE) KERNEL_FLAVOR=MICRO BENCH_MODE=OFF PLATFORM=QEMU_VIRT SMOKE=0 build
	./scripts/run_qemu.sh

bench-mono-qemu:
	$(MAKE) clean
	$(MAKE) KERNEL_FLAVOR=MONO BENCH_MODE=LATENCY PLATFORM=QEMU_VIRT SMOKE=0 build
	PROTO_OS_SILENCE_WSL_WARNING=1 ./scripts/run_qemu.sh | stdbuf -o0 tr -d '\000'

bench-micro-qemu:
	$(MAKE) clean
	$(MAKE) KERNEL_FLAVOR=MICRO BENCH_MODE=LATENCY PLATFORM=QEMU_VIRT SMOKE=0 build
	PROTO_OS_SILENCE_WSL_WARNING=1 ./scripts/run_qemu.sh | stdbuf -o0 tr -d '\000'

recovery-micro-qemu:
	$(MAKE) clean
	$(MAKE) KERNEL_FLAVOR=MICRO BENCH_MODE=RECOVERY PLATFORM=QEMU_VIRT SMOKE=0 build
	PROTO_OS_SILENCE_WSL_WARNING=1 ./scripts/run_qemu.sh | stdbuf -o0 tr -d '\000'

pi4-smoke-build:
	$(MAKE) clean
	$(MAKE) KERNEL_FLAVOR=MONO BENCH_MODE=OFF PLATFORM=PI4 SMOKE=1 build

clean:
	rm -rf $(BUILD_DIR)
