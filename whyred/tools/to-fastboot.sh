#!/bin/bash
# Get the phone into fastboot without touching it, if it runs Linux (Halium/Void) with SSH.
# Void's reboot cannot pass a reason, so call reboot(2) with LINUX_REBOOT_CMD_RESTART2 "bootloader".
# The msm restart driver then sets the fastboot flag and does a warm reset.
#   whyred/tools/to-fastboot.sh [timeout_s]
set -uo pipefail
PHONE=${PHONE:-root@10.15.19.82}
T=${1:-120}

if timeout 3 fastboot devices | grep -q fastboot; then
	echo "already in fastboot"
	exit 0
fi
if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$PHONE" true 2>/dev/null; then
	echo "phone not in fastboot and not reachable over SSH" >&2
	exit 1
fi
# sync, then sysrq 's' (sync) and 'u' (remount all read-only) so filesystems are clean.
ssh -o BatchMode=yes "$PHONE" 'sync; echo s > /proc/sysrq-trigger; echo u > /proc/sysrq-trigger; sleep 1;
python3 -c "
import ctypes
libc = ctypes.CDLL(None, use_errno=True)
# reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, arg), syscall 142 on arm64
libc.syscall(142, 0xfee1dead, 672274793, 0xA1B2C3D4, ctypes.c_char_p(b\"bootloader\"))
"' >/dev/null 2>&1 &
echo "rebooting to bootloader, waiting up to ${T}s"
if timeout "$T" fastboot getvar product 2>&1 | grep -q "product:"; then
	echo "in fastboot"
	exit 0
fi
echo "no fastboot after ${T}s" >&2
exit 1
