#!/usr/bin/env python3
"""Generate minhello.com — a 14-byte hand-assembled DOS .COM that
prints "hi\r\n$" via INT 21h AH=9 and exits via INT 20h.

No MZ header, no cdos.obj, no LSI-C86 runtime -- this is the bare
minimum that a DOS machine can execute. If this hangs on the real
MS-DOS 2.0 machine, the problem is deeper than the LSI-C86 startup.
"""
# COM files load at CS:0x100, so the msg offset in the segment is
# 0x100 + (offset of msg within the file).
code = bytes([
    0xB4, 0x09,             # MOV AH, 09
    0xBA, 0x09, 0x01,       # MOV DX, 0x0109  (msg offset when loaded)
    0xCD, 0x21,             # INT 21h          -- print $-terminated string
    0xCD, 0x20,             # INT 20h          -- terminate program (DOS 1.x+)
])
msg = b"hi\r\n$"
with open("minhello.com", "wb") as f:
    f.write(code + msg)
print(f"wrote minhello.com ({len(code) + len(msg)} bytes)")
