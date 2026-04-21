#!/usr/bin/env python3
"""Patch an LSI-C86 .EXE so that cdos.obj's FPU detection block is
skipped entirely.

cdos.obj's startup contains:
  FNINIT            DB E3              ; at code offset 0xB7
  MOV [0050], 5A5A  C7 06 50 00 5A 5A
  FNSTSW [0050]     DD 3E 50 00
  CMP byte [0050],0 80 3E 50 00 00
  JNE +0x1D         75 1D              ; branch to "no FPU" at 0xE7
  FNSTCW [0050]     D9 3E 50 00
  ...
  (eventually falls to 0xE7: MOV [0050], 0000)

If the VM does not emulate x87 ESC opcodes, any of these may hang.
We replace the leading FNINIT (DB E3) with JMP short +0x2E, jumping
directly to the "no FPU" path at code offset 0xE7.
"""
import sys, shutil, struct

src = sys.argv[1]
dst = sys.argv[2]
shutil.copy(src, dst)

with open(dst, "r+b") as f:
    data = f.read()

# Locate MZ header to skip to code
assert data[:2] == b"MZ"
cparhdr = struct.unpack_from("<H", data, 8)[0]
header_size = cparhdr * 16      # 512 for LSI-C86

# cs:ip entry point
ip = struct.unpack_from("<H", data, 0x14)[0]
cs = struct.unpack_from("<H", data, 0x16)[0]
entry_file = header_size + cs * 16 + ip

# Find FNINIT (DB E3) near the entry point. Scan within the first 4KB
# after entry to locate the first occurrence.
scan_end = min(entry_file + 0x1000, len(data) - 1)
fninit_off = None
for i in range(entry_file, scan_end):
    if data[i] == 0xDB and data[i + 1] == 0xE3:
        fninit_off = i
        break
if fninit_off is None:
    sys.exit("FNINIT (DB E3) not found near entry point")

# Find the "no FPU" fallback label: scan forward for
#   C7 06 50 00 00 00    MOV [0050], 0000
# This is the MOV at 0xE7 in the original.
noftarget = None
for i in range(fninit_off, scan_end):
    if data[i:i + 6] == b"\xC7\x06\x50\x00\x00\x00":
        # This is "MOV [0050], 5A5A" shows up too (5A5A not 0000), so
        # only the 0000 constant matches.
        noftarget = i
        break
if noftarget is None:
    sys.exit("no-FPU fallback (MOV [0050],0) not found")

rel = noftarget - (fninit_off + 2)
if not (-128 <= rel <= 127):
    sys.exit(f"JMP out of short range: rel={rel}")

patched = bytearray(data)
patched[fninit_off] = 0xEB          # JMP short
patched[fninit_off + 1] = rel & 0xFF

with open(dst, "wb") as f:
    f.write(patched)
print(f"patched {dst}: JMP short +{rel} at file offset {fninit_off:#x}"
      f" (FNINIT → skip FPU check, jump to {noftarget:#x})")
