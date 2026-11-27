/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

SECTIONS
{
    .text 0x1FFEF000 : { *(.text.start) }
    .mailbox 0x1FFEF200 : { *(.mailbox) }
    ASSERT(SIZEOF(.text) <= 0x200, "ARM11 bridge overlaps the mailbox")
    ASSERT(SIZEOF(.mailbox) == 28, "Framebuffer mailbox size changed")
}
