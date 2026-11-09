#!/usr/bin/env python3
"""Physical layout of the iOS root file on the phone's ext4 userdata, for the eMMC driver.

  extents.py OUT.json [PATH_ON_PHONE]      (default /userdata/iosroot.img)

Runs `filefrag -v` over SSH, converts filesystem blocks to 512-byte sectors relative to the
userdata partition (ext4 sits directly on the partition, so block 0 = partition sector 0),
merges physically adjacent extents and checks that the extents cover the whole file in order.
mkadt_whyred.py puts the list in /sweet-emmc root-extents (env WHYRED_EXTENTS=OUT.json).
"""
import json
import os
import re
import subprocess
import sys

PHONE = os.environ.get('PHONE', 'root@10.15.19.82')
out = sys.argv[1]
path = sys.argv[2] if len(sys.argv) > 2 else '/userdata/iosroot.img'

text = subprocess.run(['ssh', '-o', 'BatchMode=yes', PHONE, f'sync; filefrag -v {path}'],
                      check=True, capture_output=True, text=True).stdout
m = re.search(r'is (\d+) \((\d+) blocks of (\d+) bytes\)', text)
if not m:
    sys.exit('unexpected filefrag output:\n' + text)
size, nblocks, bsize = map(int, m.groups())
spb = bsize // 512

ext = []
expect_logical = 0
for line in text.splitlines():
    r = re.match(r'\s*\d+:\s+(\d+)\.\.\s*(\d+):\s+(\d+)\.\.\s*(\d+):\s+(\d+):(.*)', line)
    if not r:
        continue
    lstart, lend, pstart, pend, length = map(int, r.groups()[:5])
    flags = r.group(6)
    if lstart != expect_logical or lend - lstart + 1 != length or pend - pstart + 1 != length:
        sys.exit(f'gap or inconsistency at: {line}')
    if 'unwritten' in flags:
        sys.exit(f'unwritten extent (write the whole file first): {line}')
    expect_logical = lend + 1
    start, n = pstart * spb, length * spb
    if ext and ext[-1][0] + ext[-1][1] == start:
        ext[-1][1] += n
    else:
        ext.append([start, n])

if expect_logical * bsize < size:
    sys.exit(f'extents cover {expect_logical} blocks, file has {nblocks}')
json.dump(ext, open(out, 'w'))
total = sum(n for _, n in ext)
print(f'{path}: {size} bytes, {len(ext)} extents, {total * 512 >> 20} MiB, first sector {ext[0][0]:#x}')
