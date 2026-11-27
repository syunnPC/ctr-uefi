/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Uefi.h>
#include <CtrFramebufferHandoff.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include "ConsoleLayout.h"
#include <CtrFont.h>

#define CTR_HID_PAD            0x10146000
#define CTR_TOP_BUFFER_SELECT  0x10400478
#define CTR_TOP_BUFFER_SIZE    (CTR_SCREEN_WIDTH * CTR_SCREEN_HEIGHT * 3)
#define CTR_VRAM_BASE          0x18000000
#define CTR_VRAM_END           0x18600000

STATIC volatile UINT8               *mLeft;
STATIC volatile UINT8               *mRight;
STATIC UINT16                       mLastPad;
STATIC UINT16                       mPendingPad;
STATIC EFI_SIMPLE_TEXT_OUTPUT_MODE  mTextMode = { 1, 0, EFI_LIGHTGRAY, 0, 0, FALSE };
STATIC EFI_HANDLE                   mHandle;

STATIC CONST EFI_INPUT_KEY  mPadKeys[] = {
  { SCAN_NULL,      CHAR_CARRIAGE_RETURN }, // A
  { SCAN_ESC,       0                    }, // B
  { SCAN_F2,        0                    }, // Select
  { SCAN_F1,        0                    }, // Start
  { SCAN_RIGHT,     0                    },
  { SCAN_LEFT,      0                    },
  { SCAN_UP,        0                    },
  { SCAN_DOWN,      0                    },
  { SCAN_PAGE_DOWN, 0                    }, // R
  { SCAN_PAGE_UP,   0                    }, // L
  { SCAN_HOME,      0                    }, // X
  { SCAN_END,       0                    } // Y
};

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} CTR_CONSOLE_DEVICE_PATH;
#pragma pack()

STATIC CTR_CONSOLE_DEVICE_PATH  mDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        sizeof (VENDOR_DEVICE_PATH), 0
      }
    },
    {
      0xe07f5176, 0x9f9e, 0x4311,
      {
        0xab, 0x80, 0x02, 0xc5, 0xe0, 0xd8, 0xb2, 0x45
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
VOID
WritePixel (
  UINTN                          X,
  UINTN                          Y,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel
  )
{
  UINTN  Offset;

  Offset            = CtrPixelOffset (X, Y);
  mLeft[Offset]     = Pixel.Blue;
  mLeft[Offset + 1] = Pixel.Green;
  mLeft[Offset + 2] = Pixel.Red;
  if (mRight != mLeft) {
    mRight[Offset]     = Pixel.Blue;
    mRight[Offset + 1] = Pixel.Green;
    mRight[Offset + 2] = Pixel.Red;
  }
}

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
ReadPixel (
  UINTN  X,
  UINTN  Y
  )
{
  UINTN                          Offset;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;

  Offset         = CtrPixelOffset (X, Y);
  Pixel.Blue     = mLeft[Offset];
  Pixel.Green    = mLeft[Offset + 1];
  Pixel.Red      = mLeft[Offset + 2];
  Pixel.Reserved = 0;
  return Pixel;
}

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
TextColor (
  UINTN  Color
  )
{
  STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Palette[] = {
    { 0,  0,   0,   0 }, { 170, 0,   0,   0 },
    { 0,  170, 0,   0 }, { 170, 170, 0,   0 },
    { 0,  0,   170, 0 }, { 170, 0,   170, 0 },
    { 0,  85,  170, 0 }, { 170, 170, 170, 0 },
    { 85, 85,  85,  0 }, { 255, 85,  85,  0 },
    { 85, 255, 85,  0 }, { 255, 255, 85,  0 },
    { 85, 85,  255, 0 }, { 255, 85,  255, 0 },
    { 85, 255, 255, 0 }, { 255, 255, 255, 0 }
  };

  return Palette[Color];
}

STATIC
VOID
FillRect (
  UINTN                          X,
  UINTN                          Y,
  UINTN                          Width,
  UINTN                          Height,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel
  )
{
  UINTN  Column;
  UINTN  Row;

  for (Column = X; Column < X + Width; ++Column) {
    for (Row = Y; Row < Y + Height; ++Row) {
      WritePixel (Column, Row, Pixel);
    }
  }
}

STATIC
VOID
ToggleCursor (
  VOID
  )
{
  UINTN                          X;
  UINTN                          Y;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;

  if (!mTextMode.CursorVisible) {
    return;
  }

  Y = mTextMode.CursorRow * CTR_CELL_HEIGHT + CTR_CELL_HEIGHT - 1;
  for (X = mTextMode.CursorColumn * CTR_GLYPH_WIDTH;
       X < (UINTN)(mTextMode.CursorColumn + 1) * CTR_GLYPH_WIDTH;
       ++X)
  {
    Pixel        = ReadPixel (X, Y);
    Pixel.Blue  ^= 0xff;
    Pixel.Green ^= 0xff;
    Pixel.Red   ^= 0xff;
    WritePixel (X, Y, Pixel);
  }
}

STATIC
VOID
TextNewLine (
  VOID
  )
{
  UINTN  X;
  UINTN  Y;

  if (mTextMode.CursorRow + 1 < CTR_TEXT_ROWS) {
    ++mTextMode.CursorRow;
    return;
  }

  for (X = 0; X < CTR_SCREEN_WIDTH; ++X) {
    for (Y = 0; Y < (CTR_TEXT_ROWS - 1) * CTR_CELL_HEIGHT; ++Y) {
      WritePixel (X, Y, ReadPixel (X, Y + CTR_CELL_HEIGHT));
    }
  }

  FillRect (
    0,
    (CTR_TEXT_ROWS - 1) * CTR_CELL_HEIGHT,
    CTR_SCREEN_WIDTH,
    CTR_CELL_HEIGHT,
    TextColor (mTextMode.Attribute >> 4)
    );
}

STATIC
VOID
DrawGlyph (
  CHAR16  Character
  )
{
  UINT32                         Bits;
  UINTN                          X;
  UINTN                          Y;
  UINTN                          Column;
  UINTN                          Row;
  UINTN                          RowBits;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Background;

  Bits       = mFont[Character - L' '];
  Foreground = TextColor (mTextMode.Attribute & 15);
  Background = TextColor (mTextMode.Attribute >> 4);
  Column     = mTextMode.CursorColumn * CTR_GLYPH_WIDTH;
  Row        = mTextMode.CursorRow * CTR_CELL_HEIGHT;
  for (Y = 0; Y < CTR_CELL_HEIGHT; ++Y) {
    RowBits = Y < CTR_GLYPH_HEIGHT ? (Bits >> ((CTR_GLYPH_HEIGHT - 1 - Y) * 4)) & 15 : 0;
    for (X = 0; X < CTR_GLYPH_WIDTH; ++X) {
      WritePixel (
        Column + X,
        Row + Y,
        ((RowBits << X) & 8) ? Foreground : Background
        );
    }
  }
}

STATIC
BOOLEAN
SupportedCharacter (
  CHAR16  Character
  )
{
  return (Character >= L' ' && Character <= L'~') ||
         Character == CHAR_BACKSPACE || Character == CHAR_LINEFEED ||
         Character == CHAR_CARRIAGE_RETURN;
}

STATIC
EFI_STATUS
EFIAPI
TextOutput (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  CHAR16                           *String
  )
{
  BOOLEAN  Unknown;
  CHAR16   Character;

  if (String == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Unknown = FALSE;
  ToggleCursor ();
  while (*String != L'\0') {
    Character = *String++;
    switch (Character) {
      case CHAR_BACKSPACE:
        if (mTextMode.CursorColumn > 0) {
          --mTextMode.CursorColumn;
          DrawGlyph (L' ');
        } else if (mTextMode.CursorRow > 0) {
          --mTextMode.CursorRow;
          mTextMode.CursorColumn = CTR_TEXT_COLUMNS - 1;
          DrawGlyph (L' ');
        }

        break;
      case CHAR_LINEFEED:
        TextNewLine ();
        break;
      case CHAR_CARRIAGE_RETURN:
        mTextMode.CursorColumn = 0;
        break;
      default:
        if (!SupportedCharacter (Character)) {
          Character = L'?';
          Unknown   = TRUE;
        }

        DrawGlyph (Character);
        if (++mTextMode.CursorColumn == CTR_TEXT_COLUMNS) {
          mTextMode.CursorColumn = 0;
          TextNewLine ();
        }

        break;
    }
  }

  ToggleCursor ();
  return Unknown ? EFI_WARN_UNKNOWN_GLYPH : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextTest (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  CHAR16                           *String
  )
{
  if (String == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  while (*String != L'\0') {
    if (!SupportedCharacter (*String++)) {
      return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextQueryMode (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  UINTN                            Mode,
  UINTN                            *Columns,
  UINTN                            *Rows
  )
{
  if ((Columns == NULL) || (Rows == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Mode != 0) {
    return EFI_UNSUPPORTED;
  }

  *Columns = CTR_TEXT_COLUMNS;
  *Rows    = CTR_TEXT_ROWS;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextClear (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  FillRect (0, 0, CTR_SCREEN_WIDTH, CTR_SCREEN_HEIGHT, TextColor (mTextMode.Attribute >> 4));
  mTextMode.CursorColumn = 0;
  mTextMode.CursorRow    = 0;
  ToggleCursor ();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextSetMode (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  UINTN                            Mode
  )
{
  if (Mode != 0) {
    return EFI_UNSUPPORTED;
  }

  return TextClear (This);
}

STATIC
EFI_STATUS
EFIAPI
TextSetAttribute (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  UINTN                            Attribute
  )
{
  if (Attribute > 0x7f) {
    return EFI_UNSUPPORTED;
  }

  mTextMode.Attribute = (INT32)Attribute;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextReset (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  BOOLEAN                          ExtendedVerification
  )
{
  mTextMode.Attribute     = EFI_LIGHTGRAY;
  mTextMode.CursorVisible = TRUE;
  return TextClear (This);
}

STATIC
EFI_STATUS
EFIAPI
TextSetCursor (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  UINTN                            Column,
  UINTN                            Row
  )
{
  if ((Column >= CTR_TEXT_COLUMNS) || (Row >= CTR_TEXT_ROWS)) {
    return EFI_UNSUPPORTED;
  }

  ToggleCursor ();
  mTextMode.CursorColumn = (INT32)Column;
  mTextMode.CursorRow    = (INT32)Row;
  ToggleCursor ();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TextEnableCursor (
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  BOOLEAN                          Visible
  )
{
  ToggleCursor ();
  mTextMode.CursorVisible = Visible;
  ToggleCursor ();
  return EFI_SUCCESS;
}

STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  mTextOut = {
  TextReset, TextOutput,    TextTest,         TextQueryMode, TextSetMode, TextSetAttribute,
  TextClear, TextSetCursor, TextEnableCursor, &mTextMode
};

STATIC
VOID
SamplePad (
  VOID
  )
{
  UINT16  Current;

  Current      = MmioRead16 (CTR_HID_PAD);
  mPendingPad |= CtrNewButtons (mLastPad, Current);
  mLastPad     = Current;
}

STATIC
EFI_STATUS
EFIAPI
InputReset (
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  BOOLEAN                         ExtendedVerification
  )
{
  EFI_TPL  OldTpl;

  OldTpl      = gBS->RaiseTPL (TPL_NOTIFY);
  mLastPad    = MmioRead16 (CTR_HID_PAD);
  mPendingPad = 0;
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
InputReadKey (
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  EFI_INPUT_KEY                   *Key
  )
{
  EFI_TPL  OldTpl;
  UINTN    Bit;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  SamplePad ();
  for (Bit = 0; Bit < sizeof (mPadKeys) / sizeof (mPadKeys[0]); ++Bit) {
    if ((mPendingPad & (1U << Bit)) != 0) {
      mPendingPad &= (UINT16) ~(1U << Bit);
      *Key         = mPadKeys[Bit];
      gBS->RestoreTPL (OldTpl);
      return EFI_SUCCESS;
    }
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_NOT_READY;
}

STATIC
VOID
EFIAPI
WaitForKey (
  EFI_EVENT  Event,
  VOID       *Context
  )
{
  SamplePad ();
  if (mPendingPad != 0) {
    gBS->SignalEvent (Event);
  }
}

STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL  mTextIn = { InputReset, InputReadKey, NULL };

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mGraphicsInfo = {
  0, CTR_SCREEN_WIDTH, CTR_SCREEN_HEIGHT, PixelBltOnly,
  { 0,0,                0,                 0 }, CTR_SCREEN_WIDTH
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  mGraphicsMode = {
  1, 0, &mGraphicsInfo, sizeof (mGraphicsInfo), 0, 0
};

STATIC
EFI_STATUS
EFIAPI
GraphicsQueryMode (
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  UINT32                                Mode,
  UINTN                                 *SizeOfInfo,
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if ((Mode != 0) || (SizeOfInfo == NULL) || (Info == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (sizeof (mGraphicsInfo), &mGraphicsInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (mGraphicsInfo);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GraphicsSetMode (
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  UINT32                        Mode
  )
{
  if (Mode != 0) {
    return EFI_UNSUPPORTED;
  }

  FillRect (0, 0, CTR_SCREEN_WIDTH, CTR_SCREEN_HEIGHT, TextColor (EFI_BLACK));
  ToggleCursor ();
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
VideoBounds (
  UINTN  X,
  UINTN  Y,
  UINTN  Width,
  UINTN  Height
  )
{
  return X < CTR_SCREEN_WIDTH && Y < CTR_SCREEN_HEIGHT &&
         Width <= CTR_SCREEN_WIDTH - X && Height <= CTR_SCREEN_HEIGHT - Y;
}

STATIC
EFI_STATUS
EFIAPI
GraphicsBlt (
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *Buffer,
  EFI_GRAPHICS_OUTPUT_BLT_OPERATION  Operation,
  UINTN                              SourceX,
  UINTN                              SourceY,
  UINTN                              DestinationX,
  UINTN                              DestinationY,
  UINTN                              Width,
  UINTN                              Height,
  UINTN                              Delta
  )
{
  UINTN                          Column;
  UINTN                          Row;
  UINTN                          X;
  UINTN                          Y;
  UINTN                          BufferX;
  UINTN                          BufferY;
  UINTN                          BufferLastX;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BufferRow;

  if (((UINTN)Operation >= EfiGraphicsOutputBltOperationMax) || (Width == 0) || (Height == 0) ||
      ((Operation != EfiBltVideoToVideo) && (Buffer == NULL)))
  {
    return EFI_INVALID_PARAMETER;
  }

  if ((Operation != EfiBltVideoToBltBuffer) &&
      !VideoBounds (DestinationX, DestinationY, Width, Height))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (((Operation == EfiBltVideoToBltBuffer) || (Operation == EfiBltVideoToVideo)) &&
      !VideoBounds (SourceX, SourceY, Width, Height))
  {
    return EFI_INVALID_PARAMETER;
  }

  BufferX = 0;
  BufferY = 0;
  if ((Operation == EfiBltVideoToBltBuffer) || (Operation == EfiBltBufferToVideo)) {
    BufferX = Operation == EfiBltVideoToBltBuffer ? DestinationX : SourceX;
    BufferY = Operation == EfiBltVideoToBltBuffer ? DestinationY : SourceY;
    if (Delta == 0) {
      Delta = Width * sizeof (*Buffer);
    }

    if ((BufferX > MAX_UINTN / sizeof (*Buffer) - Width) || (BufferY > MAX_UINTN - Height)) {
      return EFI_INVALID_PARAMETER;
    }

    BufferLastX = (BufferX + Width) * sizeof (*Buffer);
    if ((BufferLastX > Delta) || ((BufferY + Height - 1) > (MAX_UINTN - BufferLastX) / Delta)) {
      return EFI_INVALID_PARAMETER;
    }
  }

  ToggleCursor ();
  switch (Operation) {
    case EfiBltVideoFill:
      FillRect (DestinationX, DestinationY, Width, Height, *Buffer);
      break;
    case EfiBltVideoToBltBuffer:
      for (Row = 0; Row < Height; ++Row) {
        BufferRow = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer + (BufferY + Row) * Delta);
        for (Column = 0; Column < Width; ++Column) {
          BufferRow[BufferX + Column] = ReadPixel (SourceX + Column, SourceY + Row);
        }
      }

      break;
    case EfiBltBufferToVideo:
      for (Row = 0; Row < Height; ++Row) {
        BufferRow = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer + (BufferY + Row) * Delta);
        for (Column = 0; Column < Width; ++Column) {
          WritePixel (DestinationX + Column, DestinationY + Row, BufferRow[BufferX + Column]);
        }
      }

      break;
    case EfiBltVideoToVideo:
      for (Row = 0; Row < Height; ++Row) {
        Y = DestinationY > SourceY ? Height - 1 - Row : Row;
        for (Column = 0; Column < Width; ++Column) {
          X = DestinationX > SourceX ? Width - 1 - Column : Column;
          WritePixel (DestinationX + X, DestinationY + Y, ReadPixel (SourceX + X, SourceY + Y));
        }
      }

      break;
    default:
      break;
  }

  ToggleCursor ();
  return EFI_SUCCESS;
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  mGraphics = {
  GraphicsQueryMode, GraphicsSetMode, GraphicsBlt, &mGraphicsMode
};

STATIC
BOOLEAN
ValidFramebuffer (
  UINT32  Address
  )
{
  return Address >= CTR_VRAM_BASE && Address <= CTR_VRAM_END - CTR_TOP_BUFFER_SIZE;
}

EFI_STATUS
EFIAPI
CtrConsoleEntryPoint (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  CONST volatile UINT32  *Handoff;
  UINTN                  Index;
  UINT32                 Left;
  UINT32                 Right;
  EFI_STATUS             Status;

  Handoff = (CONST volatile UINT32 *)(UINTN)MAILBOX_ADDRESS;
  if (Handoff[0] != FRAMEBUFFERS_READY) {
    return EFI_UNSUPPORTED;
  }

  Index = 1 + 3 * (MmioRead32 (CTR_TOP_BUFFER_SELECT) & 1);
  Left  = Handoff[Index];
  Right = Handoff[Index + 1];
  if (!ValidFramebuffer (Left) || !ValidFramebuffer (Right)) {
    return EFI_UNSUPPORTED;
  }

  mLeft  = (volatile UINT8 *)(UINTN)Left;
  mRight = (volatile UINT8 *)(UINTN)Right;
  InputReset (&mTextIn, FALSE);
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitForKey, NULL, &mTextIn.WaitForKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TextReset (&mTextOut, FALSE);
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &mTextIn,
                  &gEfiSimpleTextOutProtocolGuid,
                  &mTextOut,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGraphics,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mTextIn.WaitForKey);
  }

  return Status;
}
