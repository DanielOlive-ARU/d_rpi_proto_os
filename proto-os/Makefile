CROSS ?= aarch64-linux-gnu-

CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_IMG := $(BUILD_DIR)/kernel8.img

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

.PHONY: build clean mono-qemu micro-qemu

build: $(KERNEL_ELF) $(KERNEL_IMG)

$(BUILD_DIR)/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) kernel/arch/aarch64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(KERNEL_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

mono-qemu: KERNEL_FLAVOR=MONO
mono-qemu: build
	./scripts/run_qemu.sh

micro-qemu: KERNEL_FLAVOR=MICRO
micro-qemu: build
	./scripts/run_qemu.sh

clean:
	rm -rf $(BUILD_DIR)
