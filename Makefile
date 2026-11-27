# SPDX-License-Identifier: GPL-2.0-or-later

.PHONY: all test sd-test-app source-archive
all:
	+./build.sh

source-archive:
	./tools/source-archive.sh

test:
	mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror Tests/ctr_console_test.c -o build/ctr_console_test
	./build/ctr_console_test
	$(CC) -I CtrPkg/Include -std=c11 -Wall -Wextra -Werror Tests/ctr_sd_test.c -o build/ctr_sd_test
	./build/ctr_sd_test
	python3 -m unittest Tests/test_mkfirm.py

SD_TEST_APP_BUILD_DIR ?= build/sd-test
SD_TEST_APP_EDK := .deps/edk2-work
SD_TEST_APP_GENFW := $(SD_TEST_APP_EDK)/BaseTools/Source/C/bin/GenFw
CROSS_COMPILE ?= $(if $(wildcard .deps/toolchain/usr/bin/arm-none-eabi-gcc),.deps/toolchain/usr/bin/arm-none-eabi-,arm-none-eabi-)

sd-test-app: $(SD_TEST_APP_BUILD_DIR)/BOOTARM.EFI

$(SD_TEST_APP_GENFW):
	@echo "Build the firmware first to build GenFw." >&2
	@exit 1

$(SD_TEST_APP_BUILD_DIR)/boot.elf: Tests/ctr_qemu_boot.c Makefile | $(SD_TEST_APP_GENFW)
	mkdir -p "$(@D)"
	"$(CROSS_COMPILE)gcc" -mcpu=mpcore -mfloat-abi=soft -marm -O2 -ffreestanding -fshort-wchar \
		-ffile-prefix-map="$(CURDIR)=." -fno-pic -fno-asynchronous-unwind-tables -nostdlib \
		-I"$(SD_TEST_APP_EDK)/MdePkg/Include" -I"$(SD_TEST_APP_EDK)/MdePkg/Include/Arm" \
		-Wl,-Ttext=0,-e,UefiMain,--emit-relocs -o "$@" "$<"

$(SD_TEST_APP_BUILD_DIR)/BOOTARM.EFI: $(SD_TEST_APP_BUILD_DIR)/boot.elf $(SD_TEST_APP_GENFW)
	"$(SD_TEST_APP_GENFW)" -e UEFI_APPLICATION -o "$@" "$<"
