/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <CtrFont.h>
#include <CtrFramebufferHandoff.h>
#include <Library/CtrProgressLib.h>

#define VRAM_BASE     0x18000000
#define VRAM_END      0x18600000
#define WIDTH         320
#define HEIGHT        240
#define PIXEL_SIZE    3
#define MARK_WIDTH    12
#define MARK_HEIGHT   4
#define MARK_MARGIN   8
#define TEXT_COLUMNS  48

#define START_ROW      18
#define RETURN_ROW     27
#define ERROR_ROW      36
#define STAGE_ROW      45
#define ARCH_ROW       54
#define EXCEPTION_ROW  63
#define FIRMWARE_ROW   72

STATIC CONST struct {
  UINT32         Data1;
  CONST CHAR8    *Name;
} mImageNames[] = {
  { 0xb8d9777e, "ArmCpuDxe"                },
  { 0x4f68ef8f, "CtrTimer"                 },
  { 0xda4b82ec, "CtrSd"                    },
  { 0xb601f8c4, "RuntimeDxe"               },
  { 0xf80697e9, "SecurityStubDxe"          },
  { 0xf099d67f, "WatchdogTimer"            },
  { 0x42857f0a, "CapsuleRuntimeDxe"        },
  { 0x02b01ad5, "EmuVariableRuntimeDxe"    },
  { 0xfcabe6a7, "EmbeddedMonotonicCounter" },
  { 0x16036a73, "Reset"                    },
  { 0xb336f62d, "RealTimeClock"            },
  { 0x4c6e0267, "MetronomeDxe"             },
  { 0x6b38f7b4, "DiskIoDxe"                },
  { 0x1fa1f39e, "PartitionDxe"             },
  { 0xcd3bafb6, "EnglishDxe"               },
  { 0x961578fe, "Fat"                      },
  { 0x7bb4d0f5, "CtrBds"                   },
  { 0xe07f5176, "CtrConsole"               },
};

STATIC
UINT32
GetBottomFramebuffer (
  IN UINTN  Index
  )
{
  CONST volatile UINT32  *Handoff;
  UINT32                 Address;

  Handoff = (CONST volatile UINT32 *)(UINTN)MAILBOX_ADDRESS;
  if (Handoff[0] != FRAMEBUFFERS_READY) {
    return 0;
  }

  Address = Handoff[Index];
  if ((Address < VRAM_BASE) || (Address > VRAM_END - WIDTH * HEIGHT * PIXEL_SIZE)) {
    return 0;
  }

  return Address;
}

STATIC
volatile UINT8 *
GetPixel (
  IN UINT32  Address,
  IN UINTN   X,
  IN UINTN   Y
  )
{
  return (volatile UINT8 *)(UINTN)(Address + (X * HEIGHT + HEIGHT - 1 - Y) * PIXEL_SIZE);
}

STATIC
VOID
DrawLine (
  IN UINTN        Y,
  IN CONST CHAR8  *Text
  )
{
  UINTN           Index;
  UINT32          Address;
  CONST CHAR8     *Character;
  UINTN           Column;
  UINTN           Row;
  UINTN           X;
  UINTN           Glyph;
  UINT8           Level;
  volatile UINT8  *Pixel;

  for (Index = 3; Index <= 6; Index += 3) {
    Address = GetBottomFramebuffer (Index);
    if (Address == 0) {
      continue;
    }

    Character = Text;
    for (Column = 0; Column < TEXT_COLUMNS; Column++) {
      Glyph = 0;
      if (*Character != '\0') {
        Glyph = mFont[*Character++ - ' '];
      }

      for (X = 0; X < 5; X++) {
        Pixel = GetPixel (Address, MARK_MARGIN + Column * 5 + X, Y);
        for (Row = 0; Row < 7; Row++, Pixel -= PIXEL_SIZE) {
          Level    = X < 4 && (Glyph & (1u << (27 - Row * 4 - X))) != 0 ? 0xFF : 0;
          Pixel[0] = Level;
          Pixel[1] = Level;
          Pixel[2] = Level;
        }
      }
    }
  }
}

STATIC
CHAR8 *
AppendText (
  IN CHAR8        *Destination,
  IN CONST CHAR8  *Text
  )
{
  while (*Text != '\0') {
    *Destination++ = *Text++;
  }

  return Destination;
}

STATIC
CHAR8 *
AppendHex (
  IN CHAR8   *Destination,
  IN UINT32  Value
  )
{
  STATIC CONST CHAR8  Digits[] = "0123456789ABCDEF";
  INTN                Shift;

  for (Shift = 28; Shift >= 0; Shift -= 4) {
    *Destination++ = Digits[(Value >> Shift) & 0xF];
  }

  return Destination;
}

STATIC
CHAR8 *
AppendImageName (
  IN CHAR8           *Destination,
  IN CONST EFI_GUID  *File
  )
{
  UINTN  Index;

  for (Index = 0; Index < sizeof (mImageNames) / sizeof (mImageNames[0]); Index++) {
    if (File->Data1 == mImageNames[Index].Data1) {
      return AppendText (Destination, mImageNames[Index].Name);
    }
  }

  return AppendHex (Destination, File->Data1);
}

STATIC
VOID
DrawImage (
  IN UINTN           Y,
  IN CONST CHAR8     *Prefix,
  IN CONST EFI_GUID  *File,
  IN EFI_STATUS      Status
  )
{
  CHAR8  Line[TEXT_COLUMNS + 1];
  CHAR8  *End;

  End = AppendImageName (AppendText (Line, Prefix), File);
  if (Y != START_ROW) {
    *End++ = ' ';
    End    = AppendHex (End, (UINT32)Status);
  }

  *End = '\0';
  DrawLine (Y, Line);
}

STATIC
VOID
DrawBootMarker (
  IN CTR_BOOT_PROGRESS  Stage
  )
{
  UINTN           Index;
  UINT32          Address;
  UINTN           StartX;
  UINTN           X;
  UINTN           Y;
  volatile UINT8  *Pixel;

  StartX = MARK_MARGIN + Stage * (MARK_WIDTH + 4);
  for (Index = 3; Index <= 6; Index += 3) {
    Address = GetBottomFramebuffer (Index);
    if (Address == 0) {
      continue;
    }

    for (X = StartX; X < StartX + MARK_WIDTH; X++) {
      for (Y = MARK_MARGIN; Y < MARK_MARGIN + MARK_HEIGHT; Y++) {
        Pixel    = GetPixel (Address, X, Y);
        Pixel[0] = Stage == CtrProgressSec ? 0xE0 : 0x50;
        Pixel[1] = 0xC0;
        Pixel[2] = 0x20;
      }
    }
  }
}

VOID
EFIAPI
CtrShowBootProgress (
  IN CTR_BOOT_PROGRESS  Stage,
  IN EFI_STATUS         Status
  )
{
  CHAR8  Line[TEXT_COLUMNS + 1];

  switch (Stage) {
    case CtrProgressSecEntry:
      DrawLine (FIRMWARE_ROW, "CTR UEFI handoff diagnostics");
      DrawLine (STAGE_ROW, "SEC entry");
      break;
    case CtrProgressSec:
      DrawBootMarker (Stage);
      DrawLine (STAGE_ROW, "SEC MMU and caches enabled");
      break;
    case CtrProgressPrePi:
      DrawLine (STAGE_ROW, "PrePi building HOBs");
      break;
    case CtrProgressDxeLoad:
      DrawLine (STAGE_ROW, "PrePi loading DXE core");
      break;
    case CtrProgressDxeLoadReturned:
      *AppendHex (AppendText (Line, "PrePi DXE load returned "), (UINT32)Status) = '\0';
      DrawLine (STAGE_ROW, Line);
      break;
    case CtrProgressBds:
      DrawBootMarker (Stage);
      DrawLine (STAGE_ROW, "BDS entry");
      break;
  }
}

VOID
EFIAPI
CtrShowDxeProgress (
  IN CTR_DXE_PROGRESS  Stage,
  IN CONST EFI_GUID    *File OPTIONAL,
  IN EFI_STATUS        Status
  )
{
  switch (Stage) {
    case CtrDxeCore:
      DrawLine (STAGE_ROW, "DXE core");
      break;
    case CtrDxeDispatch:
      DrawLine (STAGE_ROW, "DXE dispatch");
      break;
    case CtrDxeStart:
      DrawImage (START_ROW, "Start ", File, Status);
      break;
    case CtrDxeReturn:
      DrawImage (RETURN_ROW, "Return ", File, Status);
      if (EFI_ERROR (Status)) {
        DrawImage (ERROR_ROW, "Start error ", File, Status);
      }

      break;
    case CtrDxeLoadError:
      DrawImage (ERROR_ROW, "Load error ", File, Status);
      break;
    case CtrDxeMissingArch:
      DrawLine (ARCH_ROW, "DXE architectural protocol missing");
      break;
    case CtrDxeMissingBds:
      DrawLine (STAGE_ROW, "BDS protocol missing");
      break;
    case CtrDxeBdsHandoff:
      DrawLine (STAGE_ROW, "BDS handoff");
      break;
    case CtrDxeBdsReturned:
      DrawLine (STAGE_ROW, "BDS returned");
      break;
  }
}

VOID
EFIAPI
CtrShowArmException (
  IN EFI_EXCEPTION_TYPE            Type,
  IN CONST EFI_SYSTEM_CONTEXT_ARM  *Context
  )
{
  STATIC CONST CHAR8 *CONST  Names[] = {
    "Reset", "Undefined instruction", "SVC", "Prefetch abort", "Data abort", "Reserved"
  };
  STATIC volatile BOOLEAN    Shown;
  CHAR8                      Line[TEXT_COLUMNS + 1];
  CHAR8                      *End;

  if (Shown || (Type < 0) || ((UINTN)Type >= sizeof (Names) / sizeof (Names[0]))) {
    return;
  }

  Shown = TRUE;
  End   = AppendText (Line, "Exception ");
  End   = AppendText (End, Names[Type]);
  End   = AppendText (End, " PC ");
  End   = AppendHex (End, Context->PC);
  if (Type == EXCEPT_ARM_DATA_ABORT) {
    End = AppendText (End, " DFAR ");
    End = AppendHex (End, Context->DFAR);
  }

  *End = '\0';
  DrawLine (EXCEPTION_ROW, Line);
}
