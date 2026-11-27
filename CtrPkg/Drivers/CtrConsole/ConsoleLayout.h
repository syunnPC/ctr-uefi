/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef CTR_CONSOLE_LAYOUT_H
#define CTR_CONSOLE_LAYOUT_H

#define CTR_SCREEN_WIDTH   400
#define CTR_SCREEN_HEIGHT  240
#define CTR_GLYPH_WIDTH    5
#define CTR_GLYPH_HEIGHT   7
#define CTR_CELL_HEIGHT    9
#define CTR_TEXT_COLUMNS   80
#define CTR_TEXT_ROWS      25
#define CTR_PAD_MASK       0x0fff

static UINTN
CtrPixelOffset (
  UINTN  X,
  UINTN  Y
  )
{
  return (X * CTR_SCREEN_HEIGHT + CTR_SCREEN_HEIGHT - 1 - Y) * 3;
}

static UINT16
CtrNewButtons (
  UINT16  Previous,
  UINT16  Current
  )
{
  return (UINT16)(Previous & ~Current & CTR_PAD_MASK);
}

#endif
