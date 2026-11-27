/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef CTR_SD_MAILBOX_H
#define CTR_SD_MAILBOX_H

#define CTR_SD_MAILBOX_ADDRESS  0x1FFEE000
#define CTR_SD_SECTOR_SIZE      512

/* Request is written last by ARM11 and cleared last by ARM9. */
#define CTR_SD_IDLE   0
#define CTR_SD_PROBE  1
#define CTR_SD_READ   2

#define CTR_SD_OK        0
#define CTR_SD_ABSENT    1
#define CTR_SD_IO_ERROR  2
#define CTR_SD_CHANGED   3

#ifndef __ASSEMBLER__
typedef struct {
  volatile unsigned int    Request;
  volatile unsigned int    Result;
  volatile unsigned int    Lba;
  volatile unsigned int    LastLba;
  volatile unsigned int    Generation;
  volatile unsigned int    Data[CTR_SD_SECTOR_SIZE / sizeof (unsigned int)];
} CTR_SD_MAILBOX;
#endif

#endif
