/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CtrProgressLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Bds.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

extern EFI_GUID  gCtrDiagnosticsFileGuid;

STATIC EFI_HANDLE  mBdsHandle;

STATIC
VOID
AttachConsole (
  VOID
  )
{
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *Input;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Output;

  if (EFI_ERROR (
        gBS->LocateHandleBuffer (
               ByProtocol,
               &gEfiSimpleTextInProtocolGuid,
               NULL,
               &Count,
               &Handles
               )
        ))
  {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (!EFI_ERROR (
           gBS->HandleProtocol (
                  Handles[Index],
                  &gEfiSimpleTextInProtocolGuid,
                  (VOID **)&Input
                  )
           ) &&
        !EFI_ERROR (
           gBS->HandleProtocol (
                  Handles[Index],
                  &gEfiSimpleTextOutProtocolGuid,
                  (VOID **)&Output
                  )
           ))
    {
      gST->ConsoleInHandle     = Handles[Index];
      gST->ConIn               = Input;
      gST->ConsoleOutHandle    = Handles[Index];
      gST->ConOut              = Output;
      gST->StandardErrorHandle = Handles[Index];
      gST->StdErr              = Output;
      break;
    }
  }

  gBS->FreePool (Handles);
}

STATIC
VOID
InitializeSystemTable (
  VOID
  )
{
  CONST CHAR16  *Vendor;

  Vendor                = FixedPcdGetPtr (PcdFirmwareVendor);
  gST->FirmwareVendor   = AllocateRuntimeCopyPool (StrSize (Vendor), Vendor);
  gST->FirmwareRevision = FixedPcdGet32 (PcdFirmwareRevision);
  AttachConsole ();
  gST->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gST, gST->Hdr.HeaderSize, &gST->Hdr.CRC32);
}

STATIC
EFI_STATUS
LoadDiagnostics (
  OUT EFI_HANDLE  *Image
  )
{
  EFI_HANDLE                         *Handles;
  UINTN                              Count;
  UINTN                              Index;
  EFI_STATUS                         Status;
  EFI_DEVICE_PATH_PROTOCOL           *VolumePath;
  EFI_DEVICE_PATH_PROTOCOL           *ImagePath;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FilePath;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SetDevicePathNodeLength (&FilePath.Header, sizeof (FilePath));
  FilePath.Header.Type    = MEDIA_DEVICE_PATH;
  FilePath.Header.SubType = MEDIA_PIWG_FW_FILE_DP;
  FilePath.FvFileName     = gCtrDiagnosticsFileGuid;
  Status                  = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    VolumePath = DevicePathFromHandle (Handles[Index]);
    if (VolumePath == NULL) {
      continue;
    }

    ImagePath = AppendDevicePathNode (VolumePath, &FilePath.Header);
    if (ImagePath == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      break;
    }

    Status = gBS->LoadImage (FALSE, mBdsHandle, ImagePath, NULL, 0, Image);
    gBS->FreePool (ImagePath);
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  gBS->FreePool (Handles);
  return Status;
}

STATIC
VOID
ConnectDisks (
  VOID
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;

  if (!EFI_ERROR (
         gBS->LocateHandleBuffer (
                ByProtocol,
                &gEfiBlockIoProtocolGuid,
                NULL,
                &Count,
                &Handles
                )
         ))
  {
    for (Index = 0; Index < Count; Index++) {
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }

    gBS->FreePool (Handles);
  }
}

STATIC
VOID
TryRemovableBoot (
  VOID
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_HANDLE                Image;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Path = FileDevicePath (Handles[Index], L"\\EFI\\BOOT\\BOOTARM.EFI");
    if (Path == NULL) {
      continue;
    }

    Status = gBS->LoadImage (TRUE, mBdsHandle, Path, NULL, 0, &Image);
    gBS->FreePool (Path);
    if (!EFI_ERROR (Status)) {
      EfiSignalEventReadyToBoot ();
      gBS->StartImage (Image, NULL, NULL);
      break;
    }
  }

  gBS->FreePool (Handles);
}

STATIC
VOID
EFIAPI
CtrBdsEntry (
  IN EFI_BDS_ARCH_PROTOCOL  *This
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Image;

  CtrShowBootProgress (CtrProgressBds, EFI_SUCCESS);
  InitializeSystemTable ();
  ConnectDisks ();
  TryRemovableBoot ();
  Status = LoadDiagnostics (&Image);
  if (!EFI_ERROR (Status)) {
    EfiSignalEventReadyToBoot ();
    Status = gBS->StartImage (Image, NULL, NULL);
  }

  if (gST->ConOut != NULL) {
    Print (L"Diagnostics exited (%r).\r\n", Status);
  }

  CpuDeadLoop ();
}

STATIC EFI_BDS_ARCH_PROTOCOL  mBds = { CtrBdsEntry };

EFI_STATUS
EFIAPI
CtrBdsInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  mBdsHandle = ImageHandle;
  return gBS->InstallProtocolInterface (
                &mBdsHandle,
                &gEfiBdsArchProtocolGuid,
                EFI_NATIVE_INTERFACE,
                &mBds
                );
}
