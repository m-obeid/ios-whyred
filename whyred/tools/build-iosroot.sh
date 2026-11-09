#!/bin/bash
# Build the iOS 16.7.16 (iPhone X, 20H392) HFS+ root for whyred from the images in whyred/ipsw/.
#   whyred/tools/build-iosroot.sh          (run as your user: it asks for sudo when it needs it)
#   whyred/tools/build-iosroot.sh --resume  keep an already copied image: repair, tweak, check
# Output: whyred/ipsw/ios16-root.img (16 GiB sparse, about 12 GB used). Takes 10-20 minutes.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
I=$ROOT/whyred/ipsw
OUT=$I/ios16-root.img
OS=$I/087-86698-044.dmg		# OS
SYSOS=$I/087-86593-047.dmg	# Cryptex1,SystemOS (shared cache)
APPOS=$I/087-86715-047.dmg	# Cryptex1,AppOS

for f in "$OS" "$SYSOS" "$APPOS"; do
	[ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
done
sudo -v
if [ "${1:-}" = --resume ]; then
	[ -f "$OUT" ] || { echo "no $OUT to resume" >&2; exit 1; }
	# Linux fsck often leaves "Volume header needs minor repair" right after its own repair:
	# one more repair pass, then report the read-only check without stopping on it.
	sudo fsck.hfsplus -f -y "$OUT" | tail -2 || true
	sudo fsck.hfsplus -f -n "$OUT" | tail -3 || echo "(read-only check not clean, see above)"
else
	if [ -e "$OUT" ]; then
		read -r -p "$OUT exists (a failed earlier run?). Delete it and rebuild? [y/N] " a
		[ "$a" = y ] || exit 1
		rm -f "$OUT"
	fi
	bash "$ROOT/tools/mkiosroot.sh" "$OS" "$SYSOS" "$APPOS" "$OUT" 16G ||
		echo "mkiosroot.sh exited $?: if only its last read-only fsck failed, rerun with --resume"
fi

mnt=$(mktemp -d)
sudo mount -t hfsplus -o loop,force "$OUT" "$mnt"
trap 'sudo umount "$mnt" 2>/dev/null; rmdir "$mnt"' EXIT
sudo python3 "$ROOT/tools/sweet-root-tweaks.py" "$mnt"
# Hard-linked files whose chmod failed during the copy: their modes must still be right.
echo "modes of hard-linked files (expect 755 for binaries):"
for f in sbin/launchd sbin/fsck_hfs sbin/mount_apfs usr/libexec/init_data_protection usr/lib/libMTLCapture.dylib; do
	sudo stat -c '%a %h %U %n' "$mnt/$f" 2>&1 | sed "s|$mnt/||"
done
sudo umount "$mnt"
trap - EXIT
rmdir "$mnt"
sudo chown "$(id -u):$(id -g)" "$OUT"
ls -ls "$OUT"
echo "done: $OUT"
