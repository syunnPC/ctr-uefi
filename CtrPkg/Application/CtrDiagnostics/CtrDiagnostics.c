/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

#define MAP_ROWS  16

typedef struct {
  EFI_MEMORY_DESCRIPTOR    *Buffer;
  UINTN                    Count;
  UINTN                    DescriptorSize;
} MEMORY_MAP;

STATIC
EFI_STATUS
ReadMemoryMap (
  MEMORY_MAP  *Map
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Buffer;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;

  Size   = 0;
  Buffer = NULL;
  for ( ; ;) {
    Status = gBS->GetMemoryMap (&Size, Buffer, &Key, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      break;
    }

    if (Buffer != NULL) {
      FreePool (Buffer);
    }

    if (Size > MAX_UINTN - 2 * sizeof (EFI_MEMORY_DESCRIPTOR)) {
      return EFI_OUT_OF_RESOURCES;
    }

    Size  += 2 * sizeof (EFI_MEMORY_DESCRIPTOR);
    Buffer = AllocatePool (Size);
    if (Buffer == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  if (EFI_ERROR (Status)) {
    if (Buffer != NULL) {
      FreePool (Buffer);
    }

    return Status;
  }

  if (Map->Buffer != NULL) {
    FreePool (Map->Buffer);
  }

  Map->Buffer         = Buffer;
  Map->Count          = Size / DescriptorSize;
  Map->DescriptorSize = DescriptorSize;
  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
MemoryTypeName (
  UINT32  Type
  )
{
  STATIC CONST CHAR16 *CONST  Names[] = {
    L"Reserved",           L"Loader code",  L"Loader data",  L"Boot services code",
    L"Boot services data", L"Runtime code", L"Runtime data", L"Conventional",
    L"Unusable",           L"ACPI reclaim", L"ACPI NVS",     L"MMIO",              L"MMIO port", L"PAL code"
  };

  if (Type < sizeof (Names) / sizeof (Names[0])) {
    return Names[Type];
  }

  return L"Other";
}

STATIC
VOID
PrintHeading (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out,
  CONST CHAR16                     *Title
  )
{
  Out->ClearScreen (Out);
  Out->SetAttribute (Out, EFI_LIGHTCYAN);
  Print (L"Nintendo 3DS UEFI   %s\n\n", Title);
  Out->SetAttribute (Out, EFI_LIGHTGRAY);
}

STATIC
VOID
ShowOverview (
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL     *Graphics;
  EFI_STATUS                       Status;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;

  Out = SystemTable->ConOut;
  PrintHeading (Out, L"Firmware diagnostics");
  Print (L"Firmware vendor     %s\n", SystemTable->FirmwareVendor);
  Print (L"Firmware revision   0x%08x\n", SystemTable->FirmwareRevision);
  Print (
    L"UEFI revision       %d.%02d\n",
    SystemTable->Hdr.Revision >> 16,
    SystemTable->Hdr.Revision & 0xffff
    );

  Status = gBS->HandleProtocol (
                  SystemTable->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&Graphics
                  );
  if (!EFI_ERROR (Status)) {
    Print (
      L"Top screen          %d x %d\n",
      Graphics->Mode->Info->HorizontalResolution,
      Graphics->Mode->Info->VerticalResolution
      );
  }

  Print (L"\nA  Memory map\n");
}

STATIC
VOID
ShowMemoryMap (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out,
  CONST MEMORY_MAP                 *Map,
  UINTN                            Page,
  EFI_STATUS                       Status
  )
{
  UINTN                  Index;
  EFI_MEMORY_DESCRIPTOR  *Descriptor;

  PrintHeading (Out, L"Memory map");
  if (EFI_ERROR (Status)) {
    Print (L"Could not read the memory map (%r).\n", Status);
  } else {
    Print (
      L"Page %d of %d   %d descriptors\n\n",
      Page + 1,
      (Map->Count + MAP_ROWS - 1) / MAP_ROWS,
      Map->Count
      );
    Print (L"Physical start     Pages     Attributes         Type\n");
    for (Index = Page * MAP_ROWS; Index < Map->Count && Index < (Page + 1) * MAP_ROWS; ++Index) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map->Buffer + Index * Map->DescriptorSize);
      Print (
        L"%016lx   %08lx  %016lx   %s\n",
        Descriptor->PhysicalStart,
        Descriptor->NumberOfPages,
        Descriptor->Attribute,
        MemoryTypeName (Descriptor->Type)
        );
    }
  }

  Print (L"\nD-pad / L / R  Change page    Start  Refresh    B  Overview\n");
}

EFI_STATUS
EFIAPI
UefiMain (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *In;
  EFI_INPUT_KEY                    Key;
  MEMORY_MAP                       Map = { NULL, 0, 0 };
  EFI_STATUS                       Status;
  EFI_STATUS                       MapStatus;
  UINTN                            EventIndex;
  UINTN                            Page;
  BOOLEAN                          InMap;

  Out = SystemTable->ConOut;
  In  = SystemTable->ConIn;
  if ((Out == NULL) || (In == NULL)) {
    return EFI_NOT_READY;
  }

  Out->EnableCursor (Out, FALSE);
  InMap     = FALSE;
  Page      = 0;
  MapStatus = EFI_NOT_READY;
  ShowOverview (SystemTable);
  for ( ; ;) {
    Status = gBS->WaitForEvent (1, &In->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = In->ReadKeyStroke (In, &Key);
    if (Status == EFI_NOT_READY) {
      continue;
    }

    if (EFI_ERROR (Status)) {
      break;
    }

    if (!InMap) {
      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        InMap     = TRUE;
        Page      = 0;
        MapStatus = ReadMemoryMap (&Map);
        ShowMemoryMap (Out, &Map, Page, MapStatus);
      }

      continue;
    }

    switch (Key.ScanCode) {
      case SCAN_ESC:
        InMap = FALSE;
        ShowOverview (SystemTable);
        break;
      case SCAN_F1:
        Page      = 0;
        MapStatus = ReadMemoryMap (&Map);
        ShowMemoryMap (Out, &Map, Page, MapStatus);
        break;
      case SCAN_UP:
      case SCAN_LEFT:
      case SCAN_PAGE_UP:
        if (Page > 0) {
          ShowMemoryMap (Out, &Map, --Page, MapStatus);
        }

        break;
      case SCAN_DOWN:
      case SCAN_RIGHT:
      case SCAN_PAGE_DOWN:
        if (!EFI_ERROR (MapStatus) && ((Page + 1) * MAP_ROWS < Map.Count)) {
          ShowMemoryMap (Out, &Map, ++Page, MapStatus);
        }

        break;
    }
  }

  if (Map.Buffer != NULL) {
    FreePool (Map.Buffer);
  }

  return Status;
}
