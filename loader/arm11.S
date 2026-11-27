/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <CtrFramebufferHandoff.h>
#include <CtrPlatform.h>

.syntax unified
.arm
.section .text.start, "ax", %progbits
.global _start
.type _start, %function
_start:
    cpsid   aif, #0x13

    /* Luma enters with the MMU and caches disabled. */
    ldr     r0, =MAILBOX_ADDRESS
    ldr     r2, =FRAMEBUFFERS_READY
    ldr     r3, =FRAMEBUFFERS_ABSENT
wait_for_arm9:
    ldr     r1, [r0]
    cmp     r1, r2
    cmpne   r1, r3
    bne     wait_for_arm9

    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 5
    mcr     p15, 0, r0, c7, c5, 0
    mcr     p15, 0, r0, c7, c5, 4
    ldr     pc, =CTR_FD_BASE
.ltorg
.size _start, . - _start

/* The loader copies these zeroes before launching either core. */
.section .mailbox, "aw", %progbits
.space 7 * 4
