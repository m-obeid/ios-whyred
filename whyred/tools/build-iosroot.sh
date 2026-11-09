#!/bin/bash
# Build the iOS 16.7.16 (iPhone X, 20H392) HFS+ root for whyred from the images in whyred/ipsw/.
#   whyred/tools/build-iosroot.sh          (run as your user: it asks for sudo when it needs it)
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
[ ! -e "$OUT" ] || { echo "$OUT exists, remove it first" >&2; exit 1; }

sudo -v
bash "$ROOT/tools/mkiosroot.sh" "$OS" "$SYSOS" "$APPOS" "$OUT" 16G

mnt=$(mktemp -d)
sudo mount -t hfsplus -o loop,force "$OUT" "$mnt"
trap 'sudo umount "$mnt" 2>/dev/null; rmdir "$mnt"' EXIT
sudo python3 "$ROOT/tools/sweet-root-tweaks.py" "$mnt"
sudo umount "$mnt"
trap - EXIT
rmdir "$mnt"
sudo chown "$(id -u):$(id -g)" "$OUT"
ls -ls "$OUT"
echo "done: $OUT"
