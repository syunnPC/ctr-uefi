/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <CtrPlatform.h>

.syntax unified
.arm
.text
.align 2
.global _ModuleEntryPoint
.type _ModuleEntryPoint, %function
_ModuleEntryPoint:
  cpsid   aif, #0x13
  ldr     sp, =CTR_RAM_END

  /* Use ARMv6 extended page tables, with translation through TTBR0. */
  mrc     p15, 0, r0, c1, c0, 0
  orr     r0, r0, #(1 << 23)
  mcr     p15, 0, r0, c1, c0, 0
  mov     r0, #0
  mcr     p15, 0, r0, c2, c0, 2
  mcr     p15, 0, r0, c7, c5, 4
  bl      CtrSecMain
1:
  b       1b
.size _ModuleEntryPoint, .-_ModuleEntryPoint
