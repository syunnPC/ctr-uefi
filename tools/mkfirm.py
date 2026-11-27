#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Package the CTR firmware for Luma3DS's SD payload chainloader."""

import argparse
import hashlib
import struct
from pathlib import Path

SECTOR_SIZE = 0x200
ARM9_ADDRESS = 0x08006000
ARM11_ADDRESS = 0x1FFEF000
FD_ADDRESS = 0x21000000
FD_SIZE = 0x200000
COPY_CPU = 2


def make_firm(arm9, arm11, fd):
    if not 0 < len(arm9) <= 0x2000:
        raise ValueError("ARM9 service must fit in 8 KiB")
    if not 0 < len(arm11) <= 2 * SECTOR_SIZE:
        raise ValueError("ARM11 bridge and mailbox must fit in two FIRM sectors")
    if len(fd) != FD_SIZE:
        raise ValueError(f"FD must be exactly {FD_SIZE:#x} bytes")

    header = bytearray(SECTOR_SIZE)
    struct.pack_into("<4sIII", header, 0, b"FIRM", 0, ARM11_ADDRESS, ARM9_ADDRESS)
    header[0x10] = 1  # Request Luma's screen initialization and framebuffer argv.
    sections = []
    offset = len(header)

    for index, (address, data) in enumerate(
        ((ARM9_ADDRESS, arm9), (ARM11_ADDRESS, arm11), (FD_ADDRESS, fd))
    ):
        data += bytes(-len(data) % SECTOR_SIZE)
        struct.pack_into(
            "<IIII32s",
            header,
            0x40 + index * 0x30,
            offset,
            address,
            len(data),
            COPY_CPU,
            hashlib.sha256(data).digest(),
        )
        sections.append(data)
        offset += len(data)

    return b"".join([header, *sections])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arm9", required=True, type=Path)
    parser.add_argument("--arm11", required=True, type=Path)
    parser.add_argument("--fd", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        firm = make_firm(
            args.arm9.read_bytes(), args.arm11.read_bytes(), args.fd.read_bytes()
        )
        args.output.write_bytes(firm)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
