#!/usr/bin/env python3
"""Print the phone's live kernel console, streamed by sweet_usb.c to udp/5140 on 192.168.7.1.

  udplog.py [LOGFILE] [--secs N]

The NetworkManager profile "whyred-usb" gives the laptop 192.168.7.1 when the phone's USB
Ethernet (MAC 02:00:5e:77:ee:01) appears, so no sudo is needed. Replaces netcat in
tools/sweet-usbnet.sh.
"""
import socket
import sys
import time

args = [a for a in sys.argv[1:] if not a.startswith('--')]
secs = None
if '--secs' in sys.argv:
    secs = float(sys.argv[sys.argv.index('--secs') + 1])
    args = [a for a in args if a != sys.argv[sys.argv.index('--secs') + 1]]
out = open(args[0], 'ab') if args else None

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 5140))
end = time.time() + secs if secs else None
while end is None or time.time() < end:
    s.settimeout(1 if end else None)
    try:
        data, _ = s.recvfrom(65536)
    except socket.timeout:
        continue
    sys.stdout.buffer.write(data)
    sys.stdout.flush()
    if out:
        out.write(data)
        out.flush()
