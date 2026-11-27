/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/EfiResetSystemLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

#define MCU_I2C_BASE   0x10144000
#define MCU_ADDRESS    0x4A
#define MCU_POWER_REG  0x20
#define I2C_DATA       0
#define I2C_CONTROL    1
#define I2C_START      BIT1
#define I2C_STOP       BIT0
#define I2C_PAUSE      BIT2
#define I2C_ACK        BIT4
#define I2C_BUSY       BIT7

STATIC UINTN      mI2cBase = MCU_I2C_BASE;
STATIC BOOLEAN    mRuntimeMapped;
STATIC EFI_EVENT  mVirtualEvent;

STATIC
BOOLEAN
WaitForBus (
  VOID
  )
{
  UINTN  Iteration;

  for (Iteration = 0; Iteration < 100000; Iteration++) {
    if ((MmioRead8 (mI2cBase + I2C_CONTROL) & I2C_BUSY) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
WriteI2cByte (
  IN UINT8  Data,
  IN UINT8  Control
  )
{
  MmioWrite8 (mI2cBase + I2C_DATA, Data);
  MmioWrite8 (mI2cBase + I2C_CONTROL, I2C_BUSY | Control);
  return WaitForBus () && ((MmioRead8 (mI2cBase + I2C_CONTROL) & I2C_ACK) != 0);
}

EFI_STATUS
EFIAPI
LibResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN CHAR16          *ResetData OPTIONAL
  )
{
  UINT8  Command;

  if (ResetType == EfiResetShutdown) {
    Command = BIT0;
  } else if ((ResetType == EfiResetCold) || (ResetType == EfiResetWarm)) {
    Command = BIT2;
  } else {
    return EFI_UNSUPPORTED;
  }

  if (!mRuntimeMapped && EfiAtRuntime ()) {
    return EFI_UNSUPPORTED;
  }

  if (!WaitForBus ()) {
    return EFI_DEVICE_ERROR;
  }

  if (WriteI2cByte (MCU_ADDRESS, I2C_START) &&
      WriteI2cByte (MCU_POWER_REG, 0) &&
      WriteI2cByte (Command, I2C_STOP))
  {
    CpuDeadLoop ();
  }

  if (WaitForBus ()) {
    MmioWrite8 (mI2cBase + I2C_CONTROL, I2C_BUSY | I2C_STOP | I2C_PAUSE);
    WaitForBus ();
  }

  return EFI_DEVICE_ERROR;
}

STATIC
VOID
EFIAPI
ConvertI2cPointer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mRuntimeMapped = !EFI_ERROR (EfiConvertPointer (0, (VOID **)&mI2cBase));
}

EFI_STATUS
EFIAPI
LibInitializeResetSystem (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  MCU_I2C_BASE,
                  EFI_PAGE_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_XP | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gDS->SetMemorySpaceAttributes (
                  MCU_I2C_BASE,
                  EFI_PAGE_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_XP | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ConvertI2cPointer,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualEvent
                  );
  mRuntimeMapped = !EFI_ERROR (Status);
  return Status;
}
