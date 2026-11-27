/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>
#include <CtrPlatform.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Cpu.h>
#include <Protocol/Timer.h>

#define GICC_CONTROL        (CTR_GIC_CPU + 0x000)
#define GICC_PRIORITY_MASK  (CTR_GIC_CPU + 0x004)
#define GICC_ACKNOWLEDGE    (CTR_GIC_CPU + 0x00C)
#define GICC_EOI            (CTR_GIC_CPU + 0x010)
#define GICD_CONTROL        (CTR_GIC_DIST + 0x000)
#define GICD_TYPE           (CTR_GIC_DIST + 0x004)
#define GICD_ENABLE_SET     (CTR_GIC_DIST + 0x100)
#define GICD_ENABLE_CLEAR   (CTR_GIC_DIST + 0x180)
#define GICD_PENDING_CLEAR  (CTR_GIC_DIST + 0x280)
#define GICD_PRIORITY(n)  (CTR_GIC_DIST + 0x400 + (n))
#define TIMER_LOAD     (CTR_TIMER_BASE + 0x00)
#define TIMER_CONTROL  (CTR_TIMER_BASE + 0x08)
#define TIMER_STATUS   (CTR_TIMER_BASE + 0x0C)

STATIC EFI_TIMER_NOTIFY  mNotify;
STATIC UINT64            mPeriod;
STATIC EFI_HANDLE        mHandle;
STATIC EFI_EVENT         mExitEvent;

STATIC
VOID
EFIAPI
TimerInterrupt (
  IN EFI_EXCEPTION_TYPE  ExceptionType,
  IN EFI_SYSTEM_CONTEXT  Context
  )
{
  UINT32   Acknowledge;
  UINT32   Source;
  EFI_TPL  OldTpl;

  Acknowledge = MmioRead32 (GICC_ACKNOWLEDGE);
  Source      = Acknowledge & 0x3FF;
  if (Source >= 1020) {
    return;
  }

  if (Source == CTR_TIMER_IRQ) {
    MmioWrite32 (TIMER_STATUS, 1);
  }

  MmioWrite32 (GICC_EOI, Acknowledge);
  if ((Source == CTR_TIMER_IRQ) && (mNotify != NULL)) {
    OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
    mNotify (mPeriod);
    gBS->RestoreTPL (OldTpl);
  }
}

STATIC
EFI_STATUS
EFIAPI
RegisterHandler (
  IN EFI_TIMER_ARCH_PROTOCOL  *This,
  IN EFI_TIMER_NOTIFY         Notify
  )
{
  EFI_TPL     OldTpl;
  EFI_STATUS  Status;

  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  if ((Notify != NULL) && (mNotify != NULL)) {
    Status = EFI_ALREADY_STARTED;
  } else if ((Notify == NULL) && (mNotify == NULL)) {
    Status = EFI_INVALID_PARAMETER;
  } else {
    mNotify = Notify;
    Status  = EFI_SUCCESS;
  }

  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
SetTimerPeriod (
  IN EFI_TIMER_ARCH_PROTOCOL  *This,
  IN UINT64                   Period
  )
{
  EFI_TPL  OldTpl;
  UINT64   Cycles;

  if (Period > DivU64x32 (MultU64x32 (MAX_UINT32, 10000000), CTR_PRIVATE_TIMER_HZ)) {
    return EFI_UNSUPPORTED;
  }

  Cycles = DivU64x32 (MultU64x32 (Period, CTR_PRIVATE_TIMER_HZ) + 9999999, 10000000);
  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  MmioWrite32 (TIMER_CONTROL, 0);
  MmioWrite32 (TIMER_STATUS, 1);
  MmioWrite32 (GICD_PENDING_CLEAR, BIT29);
  mPeriod = Period;
  if (Cycles != 0) {
    MmioWrite32 (TIMER_LOAD, (UINT32)Cycles);
    MmioWrite32 (TIMER_CONTROL, BIT0 | BIT1 | BIT2);
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetTimerPeriod (
  IN EFI_TIMER_ARCH_PROTOCOL  *This,
  OUT UINT64                  *Period
  )
{
  if (Period == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Period = mPeriod;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GenerateSoftInterrupt (
  IN EFI_TIMER_ARCH_PROTOCOL  *This
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_TIMER_ARCH_PROTOCOL  mTimer = {
  RegisterHandler,
  SetTimerPeriod,
  GetTimerPeriod,
  GenerateSoftInterrupt
};

STATIC
VOID
EFIAPI
StopTimer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  MmioWrite32 (TIMER_CONTROL, 0);
  MmioWrite32 (TIMER_STATUS, 1);
  MmioWrite32 (GICD_ENABLE_CLEAR, BIT29);
  MmioWrite32 (GICC_CONTROL, 0);
  MmioWrite32 (GICD_CONTROL, 0);
}

EFI_STATUS
EFIAPI
CtrTimerEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu;
  EFI_STATUS             Status;
  UINTN                  Index;
  UINTN                  Groups;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  StopTimer (NULL, NULL);
  Groups = (MmioRead32 (GICD_TYPE) & 31) + 1;
  for (Index = 0; Index < Groups; Index++) {
    MmioWrite32 (GICD_ENABLE_CLEAR + Index * sizeof (UINT32), MAX_UINT32);
    MmioWrite32 (GICD_PENDING_CLEAR + Index * sizeof (UINT32), MAX_UINT32);
  }

  Status = Cpu->RegisterInterruptHandler (Cpu, EXCEPT_ARM_IRQ, TimerInterrupt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  StopTimer,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitEvent
                  );
  if (EFI_ERROR (Status)) {
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_ARM_IRQ, NULL);
    return Status;
  }

  MmioWrite8 (GICD_PRIORITY (CTR_TIMER_IRQ), 0x80);
  MmioWrite32 (GICC_PRIORITY_MASK, 0xF0);
  MmioWrite32 (GICD_ENABLE_SET, BIT29);
  MmioWrite32 (GICD_CONTROL, 1);
  MmioWrite32 (GICC_CONTROL, 1);
  SetTimerPeriod (&mTimer, 100000);

  Status = gBS->InstallProtocolInterface (
                  &mHandle,
                  &gEfiTimerArchProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mTimer
                  );
  if (EFI_ERROR (Status)) {
    StopTimer (NULL, NULL);
    gBS->CloseEvent (mExitEvent);
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_ARM_IRQ, NULL);
  }

  return Status;
}
