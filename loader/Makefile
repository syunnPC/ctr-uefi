# SPDX-License-Identifier: GPL-2.0-or-later

CROSS_COMPILE ?= arm-none-eabi-
BUILD_DIR ?= ../build/loader
CPPFLAGS += -I../CtrPkg/Include
PREFIX_MAP = -ffile-prefix-map="$(abspath ..)=."
HANDOFF = ../CtrPkg/Include/CtrFramebufferHandoff.h
SD_MAILBOX = ../CtrPkg/Include/CtrSdMailbox.h
PLATFORM = ../CtrPkg/Include/CtrPlatform.h

.PHONY: all
all: $(BUILD_DIR)/arm9.bin $(BUILD_DIR)/arm11.bin

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/arm9.elf: arm9.S SdService.c SdTmio.c arm9.ld $(HANDOFF) $(SD_MAILBOX) Makefile | $(BUILD_DIR)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) $(PREFIX_MAP) -Os -std=c11 -Wall -Wextra -Werror \
	    -ffreestanding -fno-unwind-tables -fno-asynchronous-unwind-tables \
	    -mcpu=arm946e-s -marm -nostdlib -Wl,-T,arm9.ld -o "$@" arm9.S SdService.c SdTmio.c

$(BUILD_DIR)/arm11.elf: arm11.S arm11.ld $(HANDOFF) $(PLATFORM) Makefile | $(BUILD_DIR)
	$(CROSS_COMPILE)gcc $(CPPFLAGS) $(PREFIX_MAP) -mcpu=mpcore -marm -nostdlib -Wl,-T,arm11.ld -o "$@" arm11.S

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(CROSS_COMPILE)objcopy -O binary "$<" "$@"
