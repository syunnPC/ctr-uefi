#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Boot a packaged CTR FIRM on QEMU's CPU-only ARM11 machine.

The host supplies Luma's framebuffer handoff and active-low HID register in
shared guest RAM. The test reads the resulting top-screen framebuffer and
checks the firmware overview, memory map, and return to the overview. QTest
pulses the ARM11 IRQ input for the timer, so UEFI's idle loop can wake up.
With --fat the host also serves a partitioned FAT32 disk through the ARM9 SD
mailbox and checks that BDS starts its BOOTARM.EFI application and falls back
to embedded diagnostics when the SD application returns.
This does not emulate Luma, ARM9, the LCD controller, or the timer hardware.
"""

import argparse
import ctypes
import mmap
import re
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

RAM_SIZE = 640 * 1024 * 1024
HID_PAD = 0x10146000
WATCHDOG_COUNTER = 0x17E00624
WATCHDOG_HZ = 134055928
TIMER_CONTROL = 0x17E00608
TIMER_IRQ = 29
GIC_ACKNOWLEDGE = 0x17E0010C
GIC_EOI = 0x17E00110
SD_MAILBOX = 0x1FFEE000
SECTOR_SIZE = 512
FRAMEBUFFER_MAILBOX = 0x1FFEF200
FRAMEBUFFERS_READY = 0x31424643
FRAMEBUFFER = 0x18300000
BOTTOM_FRAMEBUFFERS = (0x18346500, 0x18446500)
SCREEN_HEIGHT = 240
PIXEL_SIZE = 3
CELL_WIDTH = 5
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = ROOT / "CtrPkg/Include/CtrFont.h"
BOOT_TEXT = "BOOTARM.EFI started from the SD card"


def load_firm(memory, path):
    firm = path.read_bytes()
    if firm[:4] != b"FIRM" or len(firm) < 0x200:
        raise ValueError("invalid FIRM header")

    arm11_entry = struct.unpack_from("<I", firm, 8)[0]
    has_arm11 = False
    for index in range(4):
        offset, address, size = struct.unpack_from("<III", firm, 0x40 + index * 0x30)
        if size == 0:
            continue
        if offset < 0x200 or offset + size > len(firm) or address + size > RAM_SIZE:
            raise ValueError("invalid FIRM section range")
        memory[address : address + size] = firm[offset : offset + size]
        has_arm11 |= address <= arm11_entry < address + size

    if not has_arm11:
        raise ValueError("ARM11 entry lies outside the FIRM sections")
    return arm11_entry


def read_font(path):
    glyphs = [
        int(bits, 16) for bits in re.findall(r"0x[0-9a-fA-F]{7}", path.read_text())
    ]
    if len(glyphs) != 95:
        raise ValueError("invalid console font")
    return dict(zip(glyphs, map(chr, range(32, 127))))


class SmokeTest:
    def __init__(self, memory, process, qtest, font, timeout, disk):
        self.memory = memory
        self.process = process
        self.qtest = qtest
        self.font = font
        self.timeout = timeout
        self.disk = disk
        self.sd_probes = 0
        self.sd_reads = 0
        self.start_ns = time.monotonic_ns()

    def tick(self):
        elapsed = time.monotonic_ns() - self.start_ns
        counter = (0xFFFFFFFF - elapsed * WATCHDOG_HZ // 1_000_000_000) & 0xFFFFFFFF
        # A transient increase would look like a wrap to a polling guest.
        ctypes.c_uint32.from_buffer(self.memory, WATCHDOG_COUNTER).value = counter
        self.service_sd()
        if self.memory[TIMER_CONTROL] & 1:
            struct.pack_into("<I", self.memory, GIC_ACKNOWLEDGE, TIMER_IRQ)
            for level in (1, 0):
                command = f"set_irq_in /machine/unattached/device[0] unnamed-gpio-in 0 {level}\n"
                self.qtest.write(command.encode("ascii"))
                self.qtest.flush()
                if self.qtest.readline() != b"OK\n":
                    raise RuntimeError("QTest rejected an ARM11 IRQ pulse")

    def service_sd(self):
        request, _, lba = struct.unpack_from("<III", self.memory, SD_MAILBOX)
        if request == 0:
            return
        if request == 1:
            self.sd_probes += 1
        elif request == 2:
            self.sd_reads += 1

        last_lba = 0
        if self.disk is None:
            result = 1  # No media
        else:
            last_lba = len(self.disk) // SECTOR_SIZE - 1
            if request == 1:
                result = 0
            elif request == 2 and lba <= last_lba:
                start = SD_MAILBOX + 20
                offset = lba * SECTOR_SIZE
                self.memory[start : start + SECTOR_SIZE] = self.disk[
                    offset : offset + SECTOR_SIZE
                ]
                result = 0
            else:
                result = 2  # I/O error
        struct.pack_into("<I", self.memory, SD_MAILBOX + 4, result)
        struct.pack_into("<II", self.memory, SD_MAILBOX + 12, last_lba, 1)
        struct.pack_into("<I", self.memory, SD_MAILBOX, 0)

    def screen_line(self, row):
        characters = []
        for column in range(80):
            bits = 0
            for glyph_row in range(7):
                y = row * 9 + glyph_row
                for glyph_column in range(4):
                    x = column * CELL_WIDTH + glyph_column
                    pixel = (
                        FRAMEBUFFER
                        + (x * SCREEN_HEIGHT + SCREEN_HEIGHT - 1 - y) * PIXEL_SIZE
                    )
                    bits = (bits << 1) | (self.memory[pixel] != 0)
            characters.append(self.font.get(bits, "?"))

        return "".join(characters).rstrip()

    def expect(self, row, text):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"QEMU exited with status {self.process.returncode}")
            self.tick()
            if text in self.screen_line(row):
                return
            time.sleep(0.005)

        screen = "\n".join(self.screen_line(row) for row in range(25)).rstrip()
        raise RuntimeError(
            f"LCD row {row} did not show {text!r} "
            f"({self.sd_probes} SD probes, {self.sd_reads} reads).\n{screen}"
        )

    def button(self, bit, row, text):
        struct.pack_into("<H", self.memory, HID_PAD, 0xFFFF ^ (1 << bit))
        self.expect(row, text)
        struct.pack_into("<H", self.memory, HID_PAD, 0xFFFF)

    def run(self):
        if self.disk is not None:
            self.expect(0, BOOT_TEXT)
            self.button(0, 0, "Nintendo 3DS UEFI   Firmware diagnostics")  # A
            return

        self.expect(0, "Nintendo 3DS UEFI   Firmware diagnostics")
        self.button(0, 0, "Nintendo 3DS UEFI   Memory map")  # A
        self.expect(2, "Page 1 of")
        self.button(4, 2, "Page 2 of")  # D-pad right
        self.button(1, 0, "Nintendo 3DS UEFI   Firmware diagnostics")  # B
        if struct.unpack_from("<I", self.memory, GIC_EOI)[0] != TIMER_IRQ:
            raise RuntimeError("firmware did not acknowledge the simulated timer IRQ")


def connect_qtest(process, sock, path, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited with status {process.returncode}")
        try:
            sock.connect(str(path))
            return
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.005)
    raise RuntimeError("QTest socket did not accept connections")


def create_disk(directory, cross_compile):
    efi = directory / "BOOTARM.EFI"
    command = ["make", "sd-test-app", f"SD_TEST_APP_BUILD_DIR={directory}"]
    if cross_compile is not None:
        command.append(f"CROSS_COMPILE={cross_compile}")
    subprocess.run(command, cwd=ROOT, check=True)

    disk = directory / "disk.img"
    partition_lba = 2048
    disk_sectors = 40 * 1024 * 1024 // SECTOR_SIZE
    with disk.open("wb") as disk_file:
        mbr = bytearray(SECTOR_SIZE)
        struct.pack_into(
            "<B3xB3xII", mbr, 446, 0, 0x0C, partition_lba, disk_sectors - partition_lba
        )
        mbr[510:512] = b"\x55\xaa"
        disk_file.write(mbr)
        disk_file.truncate(disk_sectors * SECTOR_SIZE)

    subprocess.run(
        ["mkfs.fat", "-F", "32", f"--offset={partition_lba}", disk], check=True
    )
    image = f"{disk}@@{partition_lba * SECTOR_SIZE}"
    subprocess.run(["mmd", "-i", image, "::/EFI", "::/EFI/BOOT"], check=True)
    subprocess.run(["mcopy", "-i", image, efi, "::/EFI/BOOT/BOOTARM.EFI"], check=True)
    return disk.read_bytes()


def run_qemu(args, disk):
    with tempfile.TemporaryDirectory(prefix="ctr-qemu-") as directory:
        ram_path = Path(directory) / "ram"
        qtest_path = Path(directory) / "qtest"
        with ram_path.open("w+b") as ram_file:
            ram_file.truncate(RAM_SIZE)
            with mmap.mmap(ram_file.fileno(), RAM_SIZE) as memory:
                entry = load_firm(memory, args.firm)
                struct.pack_into(
                    "<7I",
                    memory,
                    FRAMEBUFFER_MAILBOX,
                    FRAMEBUFFERS_READY,
                    FRAMEBUFFER,
                    FRAMEBUFFER,
                    BOTTOM_FRAMEBUFFERS[0],
                    0x18400000,
                    0x18400000,
                    BOTTOM_FRAMEBUFFERS[1],
                )
                struct.pack_into("<H", memory, HID_PAD, 0xFFFF)
                command = [
                    args.qemu,
                    "-accel",
                    "tcg",
                    "-cpu",
                    "arm11mpcore",
                    "-m",
                    "640M",
                    "-object",
                    f"memory-backend-file,id=ram,mem-path={ram_path},size=640M,share=on",
                    "-M",
                    "none,memory-backend=ram",
                    "-display",
                    "none",
                    "-serial",
                    "none",
                    "-monitor",
                    "none",
                    "-qtest",
                    f"unix:{qtest_path},server=on,wait=off",
                    "-qtest-log",
                    "/dev/null",
                    "-device",
                    f"loader,addr={entry:#x},cpu-num=0",
                ]
                with subprocess.Popen(command, stdin=subprocess.DEVNULL) as process:
                    try:
                        with socket.socket(socket.AF_UNIX) as sock:
                            sock.settimeout(args.timeout)
                            connect_qtest(process, sock, qtest_path, args.timeout)
                            with sock.makefile("rwb") as qtest:
                                SmokeTest(
                                    memory,
                                    process,
                                    qtest,
                                    read_font(DEFAULT_FONT),
                                    args.timeout,
                                    disk,
                                ).run()
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firm", type=Path)
    parser.add_argument("--qemu", default="qemu-system-arm")
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument(
        "--fat",
        action="store_true",
        help="also boot a FAT32 SD image (requires mkfs.fat, mtools and build tools)",
    )
    parser.add_argument(
        "--cross-compile", help="override the SD test application's toolchain prefix"
    )
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        run_qemu(args, disk=None)
        print("CTR ARM11 boot and diagnostics smoke test passed", flush=True)
        if args.fat:
            with tempfile.TemporaryDirectory(prefix="ctr-qemu-fat-") as directory:
                disk = create_disk(Path(directory), args.cross_compile)
            run_qemu(args, disk)
            print("CTR ARM11 FAT32 removable boot smoke test passed")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
