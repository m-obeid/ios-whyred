#!/bin/bash
# Copy logs and crash reports out of the iOS root on the phone, read-only, without copying the image.
#   whyred/tools/pull-ioslogs.sh [--shell]      (run as your user: it asks for sudo)
# The phone runs Linux. /userdata is mounted read-only over sshfs, iosroot.img is loop-mounted
# read-only as HFS+, and the interesting files land in whyred/out/ioslogs/<time>/, owned by you.
# --shell: open a root shell in the mounted iOS root instead (exit to unmount).
# Linux does not replay the HFS+ journal: files iOS wrote just before a hard reset may be missing.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PHONE=${PHONE:-root@10.15.19.82}
KEY=${KEY:-$HOME/.ssh/id_ed25519}
OUT=$ROOT/whyred/out/ioslogs/$(date +%Y%m%d-%H%M%S)

ssh -o BatchMode=yes -o ConnectTimeout=5 "$PHONE" 'test -f /userdata/iosroot.img' ||
	{ echo "phone not in Linux, or no /userdata/iosroot.img" >&2; exit 1; }

sudo -v
work=$(mktemp -d)
mkdir "$work/userdata" "$work/ios"
cleanup() {
	sudo umount "$work/ios" 2>/dev/null || true
	sudo umount "$work/userdata" 2>/dev/null || sudo fusermount3 -u "$work/userdata" 2>/dev/null || true
	rmdir "$work/ios" "$work/userdata" "$work" 2>/dev/null || true
}
trap cleanup EXIT

# As root (the loop mount needs it), with your key and known_hosts.
sudo sshfs -o ro,IdentityFile="$KEY",UserKnownHostsFile="$HOME/.ssh/known_hosts",BatchMode=yes \
	"$PHONE:/userdata" "$work/userdata"
sudo mount -t hfsplus -o loop,ro "$work/userdata/iosroot.img" "$work/ios"

if [ "${1:-}" = --shell ]; then
	echo "iOS root at $work/ios (read-only). Exit the shell to unmount."
	(cd "$work/ios" && sudo -s)
	exit 0
fi

mkdir -p "$OUT"
for d in private/var/mobile/Library/Logs private/var/root/Library/Logs private/var/log private/var/logs \
	Library/Logs private/var/db/sweet; do
	if sudo test -d "$work/ios/$d"; then
		mkdir -p "$OUT/$d"
		sudo rsync -a --no-owner --no-group --max-size=20m "$work/ios/$d/" "$OUT/$d/" || true
	fi
done
# Directory listings for orientation: what exists, sizes and times.
sudo find "$work/ios/private/var" -maxdepth 4 -newer "$work/ios/System/Library/CoreServices/SystemVersion.plist" \
	-printf '%TY-%Tm-%Td %TH:%TM %10s %p\n' 2>/dev/null | sed "s|$work/ios/||" | sort > "$OUT/var-recent.txt" || true
sudo chown -R "$(id -u):$(id -g)" "$OUT"
echo "copied to $OUT:"
find "$OUT" -type f | sed "s|$OUT/||" | head -40
echo "($(find "$OUT" -type f | wc -l) files)"
