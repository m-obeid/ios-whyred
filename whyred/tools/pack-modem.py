#!/usr/bin/env python3
"""Pack modem firmware files into one ramdisk for the shim's MSS probe (whyred/shim/mss.c).

  pack-modem.py FIRMWARE_DIR OUT.bin

Layout (little endian): "WMDM", u32 count, count x {char name[24], u32 offset, u32 size},
then each file at its offset (4-byte aligned). Files: mba.mbn, modem.mdt, modem.bNN.
"""
import os
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
names = ['mba.mbn', 'modem.mdt'] + sorted(n for n in os.listdir(src) if n.startswith('modem.b'))
head = 8 + 32 * len(names)
blobs, entries, off = [], [], (head + 3) & ~3
for n in names:
    data = open(os.path.join(src, n), 'rb').read()
    entries.append(struct.pack('<24sII', n.encode(), off, len(data)))
    blobs.append((off, data))
    off = (off + len(data) + 3) & ~3
buf = bytearray(off)
buf[0:8] = b'WMDM' + struct.pack('<I', len(names))
buf[8:head] = b''.join(entries)
for o, d in blobs:
    buf[o:o + len(d)] = d
open(out, 'wb').write(buf)
print(f'{out}: {len(names)} files, {len(buf)} bytes')
