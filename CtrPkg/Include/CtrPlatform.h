/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef CTR_PLATFORM_H
#define CTR_PLATFORM_H

#define CTR_FD_BASE         0x21000000
#define CTR_FD_SIZE         0x00200000
#define CTR_VECTOR_SECTION  0x21200000
#define CTR_TT_BASE         0x21300000
#define CTR_RAM_BASE        0x21400000
#define CTR_RAM_END         0x27800000
#define CTR_STACK_SIZE      0x00010000
#define CTR_STACK_BASE      (CTR_RAM_END - CTR_STACK_SIZE)

#define CTR_GIC_CPU           0x17E00100
#define CTR_GIC_DIST          0x17E01000
#define CTR_TIMER_BASE        0x17E00600
#define CTR_WATCHDOG_BASE     0x17E00620
#define CTR_PRIVATE_TIMER_HZ  134055928
#define CTR_TIMER_IRQ         29

#endif
