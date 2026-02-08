CROSS ?= aarch64-linux-gnu-

CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_IMG := $(BUILD_DIR)/kernel8.img
FLAVOR_STAMP := $(BUILD_DIR)/.flavor_

KERNEL_FLAVOR ?= MONO
DEBUG_TICK ?= 1
DEBUG_EARLY ?= 0

CFLAGS := -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -O2 -g
CFLAGS += -mcpu=cortex-a57 -I kernel/include
CFLAGS += -DDEBUG_TICK=$(DEBUG_TICK) -DDEBUG_EARLY=$(DEBUG_EARLY)

ifeq ($(KERNEL_FLAVOR),MONO)
CFLAGS += -DKERNEL_FLAVOR_MONO
else ifeq ($(KERNEL_FLAVOR),MICRO)
CFLAGS += -DKERNEL_FLAVOR_MICRO
endif

ASFLAGS := $(CFLAGS)
LDFLAGS := -T kernel/arch/aarch64/linker.ld

SRCS_C := $(shell find kernel -name '*.c')
SRCS_S := $(shell find kernel -name '*.S')
OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/%.o,$(SRCS_C)) \
        $(patsubst kernel/%.S,$(BUILD_DIR)/%.o,$(SRCS_S))

.PHONY: build clean mono-qemu micro-qemu flavor-check

build: flavor-check $(KERNEL_ELF) $(KERNEL_IMG)

flavor-check:
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(FLAVOR_STAMP) ] || [ "$$(cat $(FLAVOR_STAMP))" != "$(KERNEL_FLAVOR)" ]; then \
		printf "%s\n" "$(KERNEL_FLAVOR)" > $(FLAVOR_STAMP); \
	fi

$(FLAVOR_STAMP):
	@mkdir -p $(BUILD_DIR)
	@printf "%s\n" "$(KERNEL_FLAVOR)" > $@

$(BUILD_DIR)/%.o: kernel/%.c $(FLAVOR_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: kernel/%.S $(FLAVOR_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) kernel/arch/aarch64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

mono-qemu:
	$(MAKE) KERNEL_FLAVOR=MONO build
	./scripts/run_qemu.sh

micro-qemu:
	$(MAKE) KERNEL_FLAVOR=MICRO build
	./scripts/run_qemu.sh

clean:
	rm -rf $(BUILD_DIR)
