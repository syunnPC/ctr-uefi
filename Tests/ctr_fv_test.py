#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import struct
import sys
import uuid
from pathlib import Path

fd = Path(sys.argv[1]).read_bytes()
assert fd[0x28:0x2C] == b"_FVH"
assert struct.unpack_from("<Q", fd, 0x20)[0] == len(fd)
(reset,) = struct.unpack_from("<I", fd)
assert reset >> 24 in (0xEA, 0xEB)
entry = 8 + (reset & 0xFFFFFF) * 4
assert 0x48 <= entry < len(fd)
assert fd[entry : entry + 4] != b"\xff" * 4
(offset,) = struct.unpack_from("<H", fd, 0x30)
files = {}
while offset + 24 <= len(fd) and fd[offset : offset + 24] != b"\xff" * 24:
    size = int.from_bytes(fd[offset + 20 : offset + 23], "little")
    assert 24 <= size <= len(fd) - offset
    files[uuid.UUID(bytes_le=fd[offset : offset + 16])] = fd[offset + 18]
    offset = (offset + size + 7) & ~7

assert 3 in files.values(), "Missing SEC"
assert 5 in files.values(), "Missing DXE Core"
assert files[uuid.UUID("e79cc9be-6ad7-4140-8c55-2bbf500499e0")] == 9
