#!/usr/bin/env python3
"""Apple Device Tree for whyred (Redmi Note 5 Pro, SDM636), built from topaz's tools/mkadt.py.

  mkadt_whyred.py OUT.adt [--dump]

Overrides topaz's module constants with whyred addresses (whyred/research/recon/whyred-facts.md)
and drops nodes for hardware whyred lacks. Everything else (identity, /chosen policy) stays topaz's.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '../../tools'))
import adt  # noqa: E402
import mkadt  # noqa: E402

# Kernel RAM: every fixed carve-out sits below 0xa0400000 and the two DDR banks are adjacent,
# so 0xa0400000-0x17e4bffff is free. mkpayload needs a 32 MB aligned base.
mkadt.HIGH_PHYS_BASE = 0xa2000000
mkadt.HIGH_MEM_SIZE = 0xdc000000	# up to 0x17e000000

# interrupt-controller@17a00000, redistributors 0x17b00000 (stride 0x20000)
mkadt.GICD, mkadt.GICD_SIZE = 0x17a00000, 0x10000
mkadt.GICR, mkadt.GICR_SIZE = 0x17b00000, 0x100000

# cont_splash_mem, scanned by MDP pipe vig0 as xRGB8888
mkadt.FB, mkadt.FB_SIZE = 0x9d400000, 0x2400000
mkadt.FB_W, mkadt.FB_H, mkadt.FB_STRIDE = 1080, 2160, 4320

mkadt.SWEET_ECID = 0x5748595245440001	# 'WHYRED' + 1, synthetic

# Nodes for SM6225 hardware that whyred does not have at these addresses.
DROP = {'sweet-ufs'}


def build():
    root = mkadt.build(highmem=True)
    root.children = [c for c in root.children if c.name not in DROP]
    return root


if __name__ == '__main__':
    blob = adt.serialize(build())
    open(sys.argv[1], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
