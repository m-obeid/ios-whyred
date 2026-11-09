#!/bin/bash
# Small HFS+ root for testing the XNU eMMC driver: /sbin/launchd = tools/sweet-init.S, plus /dev.
#   sudo whyred/tools/mktestroot.sh [OUT]        (default whyred/out/testroot-hfs.img, 32 MiB)
# Needs hfsprogs (mkfs.hfsplus, fsck.hfsplus) and root for the loop mount.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/whyred/out/testroot-hfs.img}
INIT=$ROOT/whyred/out/sweet-init
[ -f "$INIT" ] || { echo "build $INIT first (whyred/mkboot.sh does it)" >&2; exit 1; }
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }

rm -f "$OUT"
truncate -s 32M "$OUT"
mkfs.hfsplus -s -v WhyredTest "$OUT" >/dev/null	# case-sensitive, no journal
mnt=$(mktemp -d)
mount -t hfsplus -o loop "$OUT" "$mnt"
trap 'umount "$mnt" 2>/dev/null; rmdir "$mnt"' EXIT
mkdir -p "$mnt/sbin" "$mnt/dev"
install -m 0755 -o 0 -g 0 "$INIT" "$mnt/sbin/launchd"
echo "whyred eMMC test root" > "$mnt/README"
ls -la "$mnt" "$mnt/sbin"
umount "$mnt"
trap - EXIT
rmdir "$mnt"
fsck.hfsplus -n "$OUT"
chown "${SUDO_UID:-0}:${SUDO_GID:-0}" "$OUT"
ls -l "$OUT"
