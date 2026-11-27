/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Base.h>
#include <CtrPlatform.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define WATCHDOG_LOAD     (CTR_WATCHDOG_BASE + 0x00)
#define WATCHDOG_COUNTER  (CTR_WATCHDOG_BASE + 0x04)
#define WATCHDOG_CONTROL  (CTR_WATCHDOG_BASE + 0x08)
#define WATCHDOG_DISABLE  (CTR_WATCHDOG_BASE + 0x14)

UINT64
EFIAPI
GetPerformanceCounter (
  VOID
  )
{
  if ((MmioRead32 (WATCHDOG_CONTROL) & (BIT0 | BIT1 | BIT3)) != (BIT0 | BIT1)) {
    MmioWrite32 (WATCHDOG_DISABLE, 0x12345678);
    MmioWrite32 (WATCHDOG_DISABLE, 0x87654321);
    MmioWrite32 (WATCHDOG_CONTROL, 0);
    MmioWrite32 (WATCHDOG_LOAD, MAX_UINT32);
    MmioWrite32 (WATCHDOG_CONTROL, BIT0 | BIT1);
  }

  return MmioRead32 (WATCHDOG_COUNTER);
}

UINT64
EFIAPI
GetPerformanceCounterProperties (
  OUT UINT64  *StartValue OPTIONAL,
  OUT UINT64  *EndValue OPTIONAL
  )
{
  if (StartValue != NULL) {
    *StartValue = MAX_UINT32;
  }

  if (EndValue != NULL) {
    *EndValue = 0;
  }

  return CTR_PRIVATE_TIMER_HZ;
}

UINT64
EFIAPI
GetTimeInNanoSecond (
  IN UINT64  Ticks
  )
{
  UINT32  Remainder;
  UINT64  Seconds;

  Seconds = DivU64x32Remainder (Ticks, CTR_PRIVATE_TIMER_HZ, &Remainder);
  return MultU64x32 (Seconds, 1000000000) +
         DivU64x32 (MultU64x32 (Remainder, 1000000000), CTR_PRIVATE_TIMER_HZ);
}

UINTN
EFIAPI
NanoSecondDelay (
  IN UINTN  Nanoseconds
  )
{
  UINT32  Start;
  UINT32  Ticks;

  Ticks = (UINT32)DivU64x32 (MultU64x32 (Nanoseconds, CTR_PRIVATE_TIMER_HZ) + 999999999, 1000000000);
  Start = (UINT32)GetPerformanceCounter ();
  while ((UINT32)(Start - MmioRead32 (WATCHDOG_COUNTER)) < Ticks) {
  }

  return Nanoseconds;
}

UINTN
EFIAPI
MicroSecondDelay (
  IN UINTN  Microseconds
  )
{
  UINTN  Remaining;

  Remaining = Microseconds;
  while (Remaining > 1000000) {
    NanoSecondDelay (1000000000);
    Remaining -= 1000000;
  }

  NanoSecondDelay (Remaining * 1000);
  return Microseconds;
}
