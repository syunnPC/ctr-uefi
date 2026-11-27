// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint16_t read16 (unsigned int reg);
static void write16 (unsigned int reg, uint16_t value);
static uint32_t read32 (unsigned int reg);
static void enable_controller (void);

typedef bool BOOLEAN;
typedef uint32_t UINT32;
#define STATIC static
#define IN
#define CONST const

static uint64_t GetPerformanceCounter (void);

#define CTR_SD_TMIO_TEST
#include "../loader/SdTmio.c"
#include "../CtrPkg/Drivers/CtrSd/CtrSdWait.h"

static uint16_t regs[0x110 / 2];
static bool inserted, high_capacity, stalled, command_issued, ocr_requested;
static unsigned int fifo_index;
static uint32_t expected_argument;
static CTR_SD_MAILBOX mailbox;
static uint32_t counter, counter_step, counter_reads, complete_at;

static uint64_t GetPerformanceCounter (void)
{
  if (++counter_reads == complete_at)
    mailbox.Request = CTR_SD_IDLE;
  counter -= counter_step;
  return counter;
}

static void test_mailbox_wait (void)
{
  const uint32_t start = CTR_PRIVATE_TIMER_HZ + 1;

  mailbox.Request = CTR_SD_PROBE;
  counter = 0;
  counter_step = 0;
  counter_reads = 0;
  assert (!CtrSdWaitForReply (&mailbox));
  assert (mailbox.Request == CTR_SD_PROBE);

  counter_reads = 0;
  complete_at = 3;
  assert (CtrSdWaitForReply (&mailbox));
  assert (mailbox.Request == CTR_SD_IDLE);

  mailbox.Request = CTR_SD_READ;
  counter_step = CTR_PRIVATE_TIMER_HZ;
  counter = start + counter_step;
  counter_reads = 0;
  /* A reply after the deadline should be ignored. */
  complete_at = 5;
  assert (!CtrSdWaitForReply (&mailbox));
  assert ((uint32_t)(start - counter) >= 2 * CTR_PRIVATE_TIMER_HZ);
}

static void enable_controller (void)
{
  memset (regs, 0, sizeof (regs));
  command_issued = false;
  ocr_requested = false;
}

static uint16_t read16 (unsigned int reg)
{
  if (reg == Status0)
    return regs[reg / 2] | (inserted ? CARD_INSERTED : 0);
  return regs[reg / 2];
}

static void response_set (unsigned int word, uint32_t value)
{
  regs[Response / 2 + 2 * word] = value;
  regs[Response / 2 + 2 * word + 1] = value >> 16;
}

static void write16 (unsigned int reg, uint16_t value)
{
  uint32_t argument;

  if (reg == Status0 || reg == Status1) {
    regs[reg / 2] &= value;
    return;
  }
  regs[reg / 2] = value;
  if (reg != Command)
    return;

  command_issued = true;
  assert ((regs[Port / 2] & 3) == 0);
  assert (regs[Mask0 / 2] == 0 && regs[Mask1 / 2] == 0);
  assert ((regs[DataControl32 / 2] & 0x402) == 0x402);
  if (stalled) {
    regs[Status1 / 2] = COMMAND_BUSY;
    return;
  }

  argument = regs[Argument / 2] | (uint32_t)regs[Argument / 2 + 1] << 16;
  regs[Status0 / 2] |= RESPONSE_END;

  switch (value) {
  case 0x0408:
    assert (argument == 0x1aa);
    if (high_capacity)
      response_set (0, 0x1aa);
    else
      regs[Status1 / 2] |= 0x40;
    break;
  case 0x0769:
    assert (argument == (high_capacity ? 0x40ff8000u : 0x00ff8000u));
    response_set (0, ocr_requested ? (high_capacity ? 0xc0000000u : 0x80000000u) : 0);
    ocr_requested = true;
    break;
  case 0x0403:
    response_set (0, 0x12345678);
    break;
  case 0x0609:
    assert (argument == 0x12340000);
    response_set (1, high_capacity ? 0x003fff00u : 0xffc00380u);
    response_set (2, high_capacity ? 0 : 0x00000a03);
    response_set (3, high_capacity ? 0x00400000 : 0);
    break;
  case 0x0446:
    assert (argument == 2);
    break;
  case 0x0410:
    assert (argument == CTR_SD_SECTOR_SIZE);
    break;
  case 0x3c12:
    assert (argument == expected_argument);
    assert (regs[Stop / 2] == 0x100);
    assert (regs[BlockCount / 2] == 1 && regs[BlockCount32 / 2] == 1);
    assert (regs[BlockLength32 / 2] == CTR_SD_SECTOR_SIZE);
    assert ((regs[Option / 2] & 0x8000) == 0);
    fifo_index = 0;
    regs[Status1 / 2] |= 0x0100;
    regs[DataControl32 / 2] |= 0x100;
    break;
  }
}

static uint32_t read32 (unsigned int reg)
{
  uint32_t value;

  assert (reg == Fifo32);
  assert (fifo_index < 128);
  value = expected_argument ^ fifo_index++;
  if (fifo_index == 128) {
    regs[DataControl32 / 2] &= ~0x100;
    regs[Status0 / 2] |= DATA_END;
  }
  return value;
}

static void test_card (bool hc, unsigned int expected_last_lba)
{
  unsigned int last_lba;
  unsigned int data[128];
  unsigned int lbas[] = {0, expected_last_lba};

  inserted = true;
  high_capacity = hc;
  assert (SdProbe (&last_lba) == CTR_SD_OK);
  assert (last_lba == expected_last_lba);
  for (unsigned int i = 0; i < 2; i++) {
    expected_argument = hc ? lbas[i] : lbas[i] * CTR_SD_SECTOR_SIZE;
    assert (SdRead (lbas[i], data) == CTR_SD_OK);
    for (unsigned int j = 0; j < 128; j++)
      assert (data[j] == (expected_argument ^ j));
  }
  inserted = false;
  assert (!SdPresent () && SdRemoved ());
  assert (SdRead (0, data) == CTR_SD_ABSENT);
}

int main (void)
{
  unsigned int last_lba;

  test_mailbox_wait ();
  assert (SdProbe (&last_lba) == CTR_SD_ABSENT);
  assert (!command_issued);
  test_card (false, 4194303);
  test_card (true, 16777215);

  inserted = true;
  stalled = true;
  assert (SdProbe (&last_lba) == CTR_SD_IO_ERROR);

  assert (capacity (0x3fffff00, 0, 0x00400000, &last_lba) == CTR_SD_OK);
  assert (last_lba == 0xffffffffu);
  assert (capacity (0, 0, 0x00800000, &last_lba) == CTR_SD_IO_ERROR);
  assert (capacity (0, 0x00000800, 0, &last_lba) == CTR_SD_IO_ERROR);
  return 0;
}
