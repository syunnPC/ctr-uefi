// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0-or-later

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2014-2015, Normmatt
 *
 * Alternatively, you may use this file under the GNU General Public License,
 * version 2 or any later version.
 */

#include <CtrSdMailbox.h>
#include <stdint.h>

#ifndef CTR_SD_TMIO_TEST
static inline uint16_t read16(unsigned int reg)
{
    return *(volatile uint16_t *)(0x10006000u + reg);
}

static inline void write16(unsigned int reg, uint16_t value)
{
    *(volatile uint16_t *)(0x10006000u + reg) = value;
}

static inline uint32_t read32(unsigned int reg)
{
    return *(volatile uint32_t *)(0x10006000u + reg);
}

static void enable_controller(void)
{
    volatile uint16_t *const route = (volatile uint16_t *)0x10000020u;

    /* Power the SD slot and route it to TMIO1 port 0. */
    *route = (*route & ~1u) | 0x200u;
}
#endif

enum {
    Command = 0x00,
    Port = 0x02,
    Argument = 0x04,
    Stop = 0x08,
    BlockCount = 0x0a,
    Response = 0x0c,
    Status0 = 0x1c,
    Status1 = 0x1e,
    Mask0 = 0x20,
    Mask1 = 0x22,
    Clock = 0x24,
    BlockLength = 0x26,
    Option = 0x28,
    DataControl = 0xd8,
    Reset = 0xe0,
    DataControl32 = 0x100,
    BlockLength32 = 0x104,
    BlockCount32 = 0x108,
    Fifo32 = 0x10c
};

#define CARD_INSERTED 0x0020
#define CARD_REMOVED  0x0008
#define RESPONSE_END  0x0001
#define DATA_END      0x0004
#define COMMAND_BUSY  0x4000
#define COMMAND_ERROR 0x807f
#define POLL_LIMIT    (1u << 18)

static uint32_t block_addressing;

unsigned int SdPresent(void) { return (read16(Status0) & CARD_INSERTED) != 0; }

unsigned int SdRemoved(void)
{
    return (read16(Status0) & (CARD_INSERTED | CARD_REMOVED)) != CARD_INSERTED;
}

static void clock_set(uint16_t mode)
{
    write16(Clock, read16(Clock) & ~0x100);
    write16(Clock, (read16(Clock) & ~0x2ff) | mode);
    write16(Clock, read16(Clock) | 0x100);
}

static void clear_status(void)
{
    /* Removal stays latched until the next probe resets the controller. */
    write16(Status0, CARD_REMOVED);
    write16(Status1, COMMAND_BUSY);
}

static unsigned int command(uint16_t code, uint32_t argument, volatile unsigned int *output)
{
    unsigned int remaining = POLL_LIMIT;
    uint16_t completion = code == 0 ? 0 : RESPONSE_END;
    unsigned int copied = output == 0;

    while (read16(Status1) & COMMAND_BUSY) {
        if (--remaining == 0)
            return CTR_SD_IO_ERROR;
    }

    write16(Mask0, 0);
    write16(Mask1, 0);
    clear_status();
    write16(DataControl32, (read16(DataControl32) & ~0x1800) | 0x400);
    write16(Argument, argument);
    write16(Argument + 2, argument >> 16);
    write16(Command, code);

    if (output != 0)
        completion |= DATA_END;
    for (remaining = POLL_LIMIT; remaining != 0; remaining--) {
        uint16_t status0 = read16(Status0);
        uint16_t status1 = read16(Status1);

        if (!(status0 & CARD_INSERTED))
            return CTR_SD_ABSENT;
        if (status1 & COMMAND_ERROR)
            return CTR_SD_IO_ERROR;

        if (!copied && (read16(DataControl32) & 0x100)) {
            write16(Status1, (uint16_t)~0x0100);
            for (unsigned int i = 0; i < CTR_SD_SECTOR_SIZE / 4; i++)
                output[i] = read32(Fifo32);
            write16(DataControl32, read16(DataControl32) & ~0x800);
            copied = 1;
        }
        if (copied && !(status1 & COMMAND_BUSY) && (status0 & completion) == completion)
            return CTR_SD_OK;
    }
    return CTR_SD_IO_ERROR;
}

static uint32_t response(unsigned int word)
{
    return read16(Response + 4 * word) | ((uint32_t)read16(Response + 4 * word + 2) << 16);
}

static unsigned int capacity(uint32_t csd1, uint32_t csd2, uint32_t csd3, unsigned int *last_lba)
{
    uint32_t sectors;
    uint32_t size;

    switch ((csd3 >> 22) & 3) {
    case 0: {
        unsigned int block_exp = (csd2 >> 8) & 15;
        unsigned int multiplier = (csd1 >> 7) & 7;
        if (block_exp < 9 || block_exp > 11)
            return CTR_SD_IO_ERROR;
        size = ((csd2 & 3) << 10) | (csd1 >> 22);
        sectors = (size + 1) << (multiplier + 2 + block_exp - 9);
        *last_lba = sectors - 1;
        break;
    }
    case 1:
        size = (csd1 >> 8) & 0x3fffff;
        *last_lba = (size << 10) | 0x3ff;
        break;
    default:
        return CTR_SD_IO_ERROR;
    }
    return CTR_SD_OK;
}

static void controller_init(void)
{
    enable_controller();
    write16(DataControl32, (read16(DataControl32) & ~0x1800) | 0x402);
    write16(DataControl, (read16(DataControl) & ~0x22) | 2);
    write16(BlockLength32, CTR_SD_SECTOR_SIZE);
    write16(BlockCount32, 1);
    write16(Reset, read16(Reset) & ~1);
    write16(Reset, read16(Reset) | 1);
    write16(Mask0, read16(Mask0) | 0x031d);
    write16(Mask1, read16(Mask1) | 0x837f);
    write16(0xfc, read16(0xfc) | 0xdb);
    write16(0xfe, read16(0xfe) | 0xdb);
    write16(Port, read16(Port) & ~3);
    write16(Clock, 0x20);
    write16(Option, 0xc0ee);
    write16(BlockLength, CTR_SD_SECTOR_SIZE);
    write16(Stop, 0);
    write16(Status0, 0);
    clock_set(0x80);
}

unsigned int SdProbe(unsigned int *last_lba)
{
    uint32_t hcs;
    uint32_t ocr = 0;
    uint32_t rca;
    unsigned int status;

    controller_init();
    for (unsigned int i = 0; i < (1u << 22); i++)
        __asm__ volatile("" : : : "memory");
    if (!SdPresent())
        return CTR_SD_ABSENT;

    status = command(0, 0, 0);
    if (status != CTR_SD_OK)
        return status;
    status = command(0x0408, 0x1aa, 0);
    hcs = status == CTR_SD_OK && (response(0) & 0xfff) == 0x1aa ? 1u << 30 : 0;

    for (unsigned int attempt = 0; attempt < 200; attempt++) {
        status = command(0x0437, 0, 0);
        if (status == CTR_SD_OK)
            status = command(0x0769, 0x00ff8000 | hcs, 0);
        if (status != CTR_SD_OK)
            return status;
        ocr = response(0);
        if (ocr & (1u << 31))
            break;
        for (unsigned int i = 0; i < (1u << 17); i++)
            __asm__ volatile("" : : : "memory");
    }

    if (!(ocr & (1u << 31)))
        return CTR_SD_IO_ERROR;
    block_addressing = hcs & ocr;
    if (command(0x0602, 0, 0) != CTR_SD_OK || command(0x0403, 0, 0) != CTR_SD_OK)
        return CTR_SD_IO_ERROR;
    rca = response(0) & 0xffff0000u;
    if (rca == 0 || command(0x0609, rca, 0) != CTR_SD_OK ||
        capacity(response(1), response(2), response(3), last_lba) != CTR_SD_OK)
        return CTR_SD_IO_ERROR;

    clock_set(1);
    if (command(0x0507, rca, 0) != CTR_SD_OK || command(0x0437, rca, 0) != CTR_SD_OK ||
        command(0x0446, 2, 0) != CTR_SD_OK || command(0x040d, rca, 0) != CTR_SD_OK ||
        command(0x0410, CTR_SD_SECTOR_SIZE, 0) != CTR_SD_OK)
        return CTR_SD_IO_ERROR;

    write16(Option, read16(Option) & ~0x8000);
    clock_set(0x201);
    return CTR_SD_OK;
}

unsigned int SdRead(unsigned int lba, volatile unsigned int *output)
{
    if (!SdPresent())
        return CTR_SD_ABSENT;

    if (!block_addressing && lba > (UINT32_MAX >> 9))
        return CTR_SD_IO_ERROR;

    write16(Stop, 0x100);
    write16(BlockCount32, 1);
    write16(BlockLength32, CTR_SD_SECTOR_SIZE);
    write16(BlockCount, 1);
    /* CMD18 with automatic stop and a count of one sector. */
    return command(0x3c12, block_addressing ? lba : lba << 9, output);
}
