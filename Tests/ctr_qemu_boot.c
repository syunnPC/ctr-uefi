/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>

EFI_STATUS
EFIAPI
UefiMain (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *In;
  EFI_BOOT_SERVICES                *BootServices;
  EFI_INPUT_KEY                    Key;
  EFI_STATUS                       Status;
  UINTN                            EventIndex;

  Out          = SystemTable->ConOut;
  In           = SystemTable->ConIn;
  BootServices = SystemTable->BootServices;
  if ((Out == NULL) || (In == NULL)) {
    return EFI_NOT_READY;
  }

  Out->EnableCursor (Out, FALSE);
  Out->ClearScreen (Out);
  Out->OutputString (Out, L"BOOTARM.EFI started from the SD card\r\n\r\n");
  Out->OutputString (Out, L"Press A or B to return to firmware diagnostics.");
  for ( ; ;) {
    Status = BootServices->WaitForEvent (1, &In->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = In->ReadKeyStroke (In, &Key);
    if (Status == EFI_NOT_READY) {
      continue;
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.ScanCode == SCAN_ESC)) {
      return EFI_SUCCESS;
    }
  }
}
