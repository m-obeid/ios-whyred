#!/usr/bin/env python3
"""Apple Device Tree for whyred (Redmi Note 5 Pro, SDM636), built from topaz's tools/mkadt.py.

  mkadt_whyred.py OUT.adt [--dump]

Overrides topaz's module constants with whyred addresses (whyred/research/recon/whyred-facts.md)
and drops nodes for hardware whyred lacks. Everything else (identity, /chosen policy) stays topaz's.
"""
import json
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
# disp0 (AppleCLCD) stays: patch 0962 points it at the whyred MDP5. WHYRED_NODISP=1 drops it.
DROP = {'sweet-ufs'} | ({'disp0'} if os.environ.get('WHYRED_NODISP') else set())


def prune(n):
    n.children = [prune(c) for c in n.children if c.name not in DROP]
    return n


# bsd/dev/sweet_emmc.c (patch 0961): SDCC1 host, absolute physical reg like /sweet-ufs.
EMMC, EMMC_SIZE = 0x0c0c4000, 0x1000


def set_canvas(n, w, h):
    for c in n.children:
        if c.name == 'disp0':
            c.props = [(k, f, mkadt.u32(w) if k == 'canvas-width' else mkadt.u32(h) if k == 'canvas-height' else v)
                       for k, f, v in c.props]
        set_canvas(c, w, h)


def build():
    root = prune(mkadt.build(highmem=True))
    # WHYRED_NATIVE=1 (with boot-arg sweetfb_native=1): iOS renders the real 1080x2160 panel instead of
    # the iPhone X canvas (1125x2436), whose centered crop hides the status bar and home indicator.
    if os.environ.get('WHYRED_NATIVE'):
        set_canvas(root, mkadt.FB_W, mkadt.FB_H)
    props = [
        ('AAPL,phandle', mkadt.u32(900)),
        ('compatible', mkadt.cstr('qcom,sdhci-msm-v5')),
        ('reg', mkadt.u64(EMMC, EMMC_SIZE)),
    ]
    # File-backed root: extents of the iOS root file inside userdata (tools/extents.py).
    ext_file = os.environ.get('WHYRED_EXTENTS')
    if ext_file:
        ext = json.load(open(ext_file))
        props.append(('root-extents', mkadt.u64(*[v for pair in ext for v in pair])))
    root.children.append(mkadt.node('sweet-emmc', props))
    return root


if __name__ == '__main__':
    blob = adt.serialize(build())
    open(sys.argv[1], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
