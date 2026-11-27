# SPDX-License-Identifier: GPL-2.0-or-later

import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "tools" / "mkfirm.py"


class FirmTest(unittest.TestCase):
    def package(self, arm9, arm11, fd):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("arm9", "arm11", "fd", "firm")]
            for path, data in zip(paths, (arm9, arm11, fd)):
                path.write_bytes(data)
            command = [sys.executable, str(TOOL)]
            for flag, path in zip(("--arm9", "--arm11", "--fd", "--output"), paths):
                command.extend((flag, str(path)))
            result = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
            output = paths[-1].read_bytes() if paths[-1].exists() else None
            return result, output

    def test_chainloader_layout(self):
        arm9 = b"arm9" * 129
        arm11 = b"arm11".ljust(0x200, b"\0") + bytes(7 * 4)
        fd = b"UEFI" * (0x200000 // 4)
        result, firm = self.package(arm9, arm11, fd)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(firm[:4], b"FIRM")
        self.assertEqual(
            struct.unpack_from("<III", firm, 4), (0, 0x1FFEF000, 0x08006000)
        )
        self.assertEqual(firm[0x10:0x40], b"\1" + bytes(0x2F))
        self.assertEqual(firm[0xD0:0x200], bytes(0x130))

        expected = [
            (0x200, 0x08006000, 0x400, arm9),
            (0x600, 0x1FFEF000, 0x400, arm11),
            (0xA00, 0x21000000, 0x200000, fd),
        ]
        for index, (offset, address, size, raw) in enumerate(expected):
            descriptor = 0x40 + 0x30 * index
            self.assertEqual(
                struct.unpack_from("<IIII", firm, descriptor),
                (offset, address, size, 2),
            )
            payload = firm[offset : offset + size]
            self.assertEqual(payload, raw.ljust(size, b"\0"))
            self.assertEqual(
                firm[descriptor + 16 : descriptor + 48],
                hashlib.sha256(payload).digest(),
            )
        self.assertEqual(len(firm), 0x200A00)

    def test_invalid_inputs(self):
        for arm9, arm11, fd, diagnostic in (
            (b"", b"a", bytes(0x200000), "ARM9"),
            (bytes(0x2001), b"a", bytes(0x200000), "ARM9"),
            (b"a", b"", bytes(0x200000), "ARM11"),
            (b"a", bytes(0x401), bytes(0x200000), "ARM11"),
            (b"a", b"a", b"small", "FD"),
        ):
            with self.subTest(
                diagnostic=diagnostic, sizes=(len(arm9), len(arm11), len(fd))
            ):
                result, output = self.package(arm9, arm11, fd)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)
                self.assertIsNone(output)


if __name__ == "__main__":
    unittest.main()
