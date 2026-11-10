#!/usr/bin/env python3
"""Set ArtworkDeviceSubType in a pulled MobileGestalt cache plist (CacheExtra oPeik/9e8lQWMszEjbPzng).

  mg-subtype.py IN.plist OUT.plist SUBTYPE

UIKit picks the status bar layout from this value: 2436 = iPhone X notch layout.
Push the result back with whyred/tools/mg-push.sh (or ssh cat) and restart SpringBoard.
"""
import plistlib
import sys

src, dst, sub = sys.argv[1], sys.argv[2], int(sys.argv[3])
raw = open(src, 'rb').read()
fmt = plistlib.FMT_BINARY if raw.startswith(b'bplist') else plistlib.FMT_XML
d = plistlib.loads(raw)
art = d['CacheExtra']['oPeik/9e8lQWMszEjbPzng']
print(f'ArtworkDeviceSubType {art["ArtworkDeviceSubType"]} -> {sub}')
art['ArtworkDeviceSubType'] = sub
open(dst, 'wb').write(plistlib.dumps(d, fmt=fmt))
