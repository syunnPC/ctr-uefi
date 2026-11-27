#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import re
import subprocess
import sys

assembly = subprocess.check_output([sys.argv[1], "-d", sys.argv[2]], text=True)
instructions = [
    int(word, 16)
    for word in re.findall(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", assembly, re.MULTILINE)
]
assert instructions, "Missing ARM exception instructions"

# MRC p15, 0, Rt, c6, c0, 2, ignoring the condition and target register.
assert all((word & 0x0FFF0FFF) != 0x0E160F50 for word in instructions), (
    "The exception handler reads the ARMv7 IFAR register on ARM11 MPCore"
)
