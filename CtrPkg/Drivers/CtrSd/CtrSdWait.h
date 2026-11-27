/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef CTR_SD_WAIT_H
#define CTR_SD_WAIT_H

#include <CtrPlatform.h>
#include <CtrSdMailbox.h>

#define CTR_SD_STALLED_POLL_LIMIT  (1U << 18)

STATIC
BOOLEAN
CtrSdWaitForReply (
  IN CONST volatile CTR_SD_MAILBOX  *Mailbox
  )
{
  UINT32  Start;
  UINT32  Previous;
  UINT32  Current;
  UINT32  StalledPolls;

  Start        = (UINT32)GetPerformanceCounter ();
  Previous     = Start;
  StalledPolls = 0;
  while (Mailbox->Request != CTR_SD_IDLE) {
    Current = (UINT32)GetPerformanceCounter ();
    if (Current == Previous) {
      StalledPolls++;
    } else {
      StalledPolls = 0;
      Previous     = Current;
    }

    if (((UINT32)(Start - Current) >= 2 * CTR_PRIVATE_TIMER_HZ) ||
        (StalledPolls >= CTR_SD_STALLED_POLL_LIMIT))
    {
      break;
    }
  }

  return Mailbox->Request == CTR_SD_IDLE;
}

#endif
