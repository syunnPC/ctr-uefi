/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Library/ArmLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "CtrSdWait.h"

STATIC volatile CTR_SD_MAILBOX *CONST  mMailbox =
  (volatile CTR_SD_MAILBOX *)(UINTN)CTR_SD_MAILBOX_ADDRESS;
STATIC EFI_LOCK  mLock = EFI_INITIALIZE_LOCK_VARIABLE (TPL_NOTIFY);
STATIC BOOLEAN   mTimedOut;

STATIC EFI_BLOCK_IO_MEDIA  mSdMedia = {
  0, TRUE, FALSE, FALSE, TRUE, FALSE, CTR_SD_SECTOR_SIZE, 1, 0, 0, 0, 0
};

STATIC struct {
  VENDOR_DEVICE_PATH          Controller;
  EFI_DEVICE_PATH_PROTOCOL    End;
} mSdPath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        sizeof (VENDOR_DEVICE_PATH), 0
      }
    },
    {
      0xda4b82ec, 0xc9c8, 0x476b,
      {
        0xa9, 0x2b, 0x2a, 0xd3, 0x9f, 0xab, 0x07, 0x4d
      }
    }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      sizeof (EFI_DEVICE_PATH_PROTOCOL), 0
    }
  }
};

STATIC
EFI_STATUS
Submit (
  IN UINT32  Request,
  IN UINT32  Lba
  )
{
  if (mTimedOut || (mMailbox->Request != CTR_SD_IDLE)) {
    return EFI_DEVICE_ERROR;
  }

  mMailbox->Lba = Lba;
  ArmDataMemoryBarrier ();
  mMailbox->Request = Request;
  ArmDataMemoryBarrier ();

  if (!CtrSdWaitForReply (mMailbox)) {
    mTimedOut             = TRUE;
    mSdMedia.MediaPresent = FALSE;
    return EFI_TIMEOUT;
  }

  ArmDataMemoryBarrier ();

  switch (mMailbox->Result) {
    case CTR_SD_OK:
      return EFI_SUCCESS;
    case CTR_SD_ABSENT:
      mSdMedia.MediaPresent = FALSE;
      return EFI_NO_MEDIA;
    case CTR_SD_CHANGED:
      mSdMedia.MediaPresent = FALSE;
      return EFI_MEDIA_CHANGED;
    default:
      return EFI_DEVICE_ERROR;
  }
}

STATIC
EFI_STATUS
Probe (
  VOID
  )
{
  EFI_STATUS  Status;

  Status                = Submit (CTR_SD_PROBE, 0);
  mSdMedia.MediaPresent = !EFI_ERROR (Status);
  if (mSdMedia.MediaPresent) {
    mSdMedia.LastBlock = mMailbox->LastLba;
    mSdMedia.MediaId   = mMailbox->Generation;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Reset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  EFI_STATUS  Status;

  EfiAcquireLock (&mLock);
  Status = Probe ();
  EfiReleaseLock (&mLock);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
ReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       Blocks;
  UINTN       Word;
  UINT8       *Output;
  UINT32      Data;

  if (BufferSize % CTR_SD_SECTOR_SIZE != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((BufferSize != 0) && (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  EfiAcquireLock (&mLock);
  Status = mSdMedia.MediaPresent ? EFI_SUCCESS : Probe ();
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  if (MediaId != mSdMedia.MediaId) {
    Status = EFI_MEDIA_CHANGED;
    goto Done;
  }

  Blocks = BufferSize / CTR_SD_SECTOR_SIZE;
  if (Blocks == 0) {
    goto Done;
  }

  if ((Lba > mSdMedia.LastBlock) || (Blocks - 1 > mSdMedia.LastBlock - Lba)) {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  Output = Buffer;
  while (Blocks-- != 0) {
    Status = Submit (CTR_SD_READ, (UINT32)Lba++);
    if (EFI_ERROR (Status)) {
      if (Status == EFI_MEDIA_CHANGED) {
        Status = Probe ();
        if (!EFI_ERROR (Status)) {
          Status = EFI_MEDIA_CHANGED;
        }
      }

      break;
    }

    for (Word = 0; Word < CTR_SD_SECTOR_SIZE / sizeof (Data); Word++) {
      Data      = mMailbox->Data[Word];
      *Output++ = (UINT8)Data;
      *Output++ = (UINT8)(Data >> 8);
      *Output++ = (UINT8)(Data >> 16);
      *Output++ = (UINT8)(Data >> 24);
    }
  }

Done:
  EfiReleaseLock (&mLock);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
WriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
EFIAPI
FlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC EFI_BLOCK_IO_PROTOCOL  mSdBlockIo = {
  EFI_BLOCK_IO_PROTOCOL_REVISION,
  &mSdMedia,
  Reset,
  ReadBlocks,
  WriteBlocks,
  FlushBlocks
};

EFI_STATUS
EFIAPI
CtrSdEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;

  Handle = NULL;
  Reset (&mSdBlockIo, FALSE);
  return gBS->InstallMultipleProtocolInterfaces (
                &Handle,
                &gEfiDevicePathProtocolGuid,
                &mSdPath,
                &gEfiBlockIoProtocolGuid,
                &mSdBlockIo,
                NULL
                );
}
