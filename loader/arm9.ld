/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

SECTIONS
{
    .text 0x08006000 : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { *(.bss*) }
    ASSERT(. <= 0x08008000, "ARM9 service exceeds 8 KiB")
    __stack_top = 0x08009000;
}
