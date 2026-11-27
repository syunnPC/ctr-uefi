// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdint.h>

typedef uint16_t UINT16;
typedef uintptr_t UINTN;

#include "../CtrPkg/Drivers/CtrConsole/ConsoleLayout.h"

int main (void)
{
  /* The 400x240 LCD stores packed BGR, bottom-to-top within each column. */
  assert (CtrPixelOffset (0, 239) == 0);
  assert (CtrPixelOffset (0, 0) == 717);
  assert (CtrPixelOffset (1, 239) == 720);
  assert (CtrPixelOffset (399, 0) == 287997);
  assert (CTR_TEXT_COLUMNS * CTR_GLYPH_WIDTH <= CTR_SCREEN_WIDTH);
  assert (CTR_TEXT_ROWS * CTR_CELL_HEIGHT <= CTR_SCREEN_HEIGHT);

  /* HID_PAD is active low; report newly pressed buttons only. */
  assert (CtrNewButtons (0xffff, 0xfffe) == 0x0001);
  assert (CtrNewButtons (0xfffe, 0xfffe) == 0);
  assert (CtrNewButtons (0xfffe, 0xffff) == 0);
  assert (CtrNewButtons (0xfffe, 0xeff6) == 0x0008);
  return 0;
}
