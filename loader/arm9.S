/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <CtrFramebufferHandoff.h>
#include <CtrSdMailbox.h>

.syntax unified
.arm
.section .text.start, "ax", %progbits
.global _start
.type _start, %function
_start:
    msr     cpsr_c, #0xd3
    ldr     r3, =CTR_SD_MAILBOX_ADDRESS
    mov     r4, #CTR_SD_IDLE
    str     r4, [r3]
    ldr     r3, =MAILBOX_ADDRESS
    ldr     r12, =FRAMEBUFFERS_ABSENT

    /* Luma passes two framebuffer records at argv[1]. */
    ldr     r4, =0x4beef
    cmp     r2, r4
    bne     publish
    cmp     r0, #2
    bne     publish
    cmp     r1, #0
    beq     publish
    ldr     r1, [r1, #4]
    cmp     r1, #0
    beq     publish

    ldmia   r1, {r4-r9}
    add     r0, r3, #4
    stmia   r0, {r4-r9}
    ldr     r12, =FRAMEBUFFERS_READY

publish:
    mov     r0, #0
    mcr     p15, 0, r0, c7, c10, 4
    str     r12, [r3]
    mcr     p15, 0, r0, c7, c10, 4
    ldr     sp, =__stack_top
    b       SdService
.ltorg
.size _start, . - _start
