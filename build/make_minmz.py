#!/usr/bin/env python3
"""Generate minmz.exe -- a hand-crafted MZ .EXE that only prints '!'
and exits. No relocations, no startup code, minimal header.

If this hangs on the real MS-DOS 2.0 machine, the issue is in the
MZ loader (very unlikely but possible). If it runs, we've confirmed
MZ loading works and the problem lies entirely in LSI-C86's startup.
"""
import struct

code = bytes([
    0xB4, 0x02,   # MOV AH, 02 (DOS print char)
    0xB2, 0x21,   # MOV DL, '!'
    0xCD, 0x21,   # INT 21h
    0xB4, 0x4C,   # MOV AH, 4Ch (DOS exit)
    0xCD, 0x21,   # INT 21h
])

header_size = 32                     # one paragraph
image_size  = header_size + len(code)  # 32 + 10 = 42
cparhdr     = header_size // 16      # 2
cp          = 1
cblp        = image_size             # 42 -- byte count in the only page

header = struct.pack(
    '<2sHHHHHHHHHHHHH',
    b'MZ',
    cblp, cp,
    0,                  # crlc: no relocations
    cparhdr,            # cparhdr: 2 paragraphs (32 bytes)
    0,                  # minalloc
    0xFFFF,             # maxalloc
    0,                  # ss relative to load_seg
    0x0100,             # sp (stack lives well above the 10-byte code)
    0,                  # csum
    0,                  # ip entry
    0,                  # cs relative
    0x001C,             # lfarlc (reloc table pointer, points past our header)
    0,                  # ovno
)
# pad header out to header_size
header += b'\x00' * (header_size - len(header))

with open('minmz.exe', 'wb') as f:
    f.write(header + code)
print(f"wrote minmz.exe ({image_size} bytes)")
