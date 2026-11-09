#!/bin/bash
# Copy the iOS root image into the reserved file on the phone's userdata, record its extents
# and check that the raw eMMC sectors hold the image (what XNU will read).
#   whyred/tools/push-root.sh [IMAGE]        (default whyred/ipsw/ios16-root.img)
# The phone runs Linux. /userdata/iosroot.img must exist with the image's size (fallocate).
# userdata is ext4 with data=journal: data reaches its final sectors only after a journal
# checkpoint, so the raw read below is the real test, not the copy's exit status.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMG=${1:-$ROOT/whyred/ipsw/ios16-root.img}
PHONE=${PHONE:-root@10.15.19.82}
DST=/userdata/iosroot.img
EXT=$ROOT/whyred/out/root-extents.json
SSH=(ssh -o BatchMode=yes "$PHONE")

size=$(stat -c %s "$IMG")
rsize=$("${SSH[@]}" "stat -c %s $DST")
[ "$size" = "$rsize" ] || { echo "size mismatch: image $size, phone file $rsize" >&2; exit 1; }

echo "local sha256..."
want=$(sha256sum "$IMG" | cut -d' ' -f1)
echo "  $want"

echo "copying $((size >> 20)) MiB (gzip stream)..."
t0=$(date +%s)
gzip -1 -c "$IMG" | "${SSH[@]}" "gunzip -c | dd of=$DST bs=4M conv=notrunc 2>/dev/null; sync"
echo "  copied in $(( $(date +%s) - t0 )) s"

python3 "$ROOT/whyred/tools/extents.py" "$EXT" "$DST"

raw_sha() {
	python3 - "$EXT" <<'EOF' | "${SSH[@]}" 'sh -s'
import json, sys
ext = json.load(open(sys.argv[1]))
# Extents are 512-byte sectors in multiples of 8 (4 KiB ext4 blocks): read with 4 KiB direct I/O.
cmds = ' ; '.join(f'dd if=/dev/disk/by-partlabel/userdata bs=4096 skip={s // 8} count={n // 8} iflag=direct 2>/dev/null'
                  for s, n in ext)
print(f'( {cmds} ) | sha256sum | cut -d" " -f1')
EOF
}
echo "raw eMMC sha256..."
got=$(raw_sha)
echo "  $got"
if [ "$got" != "$want" ]; then
	echo "raw sectors differ: forcing an ext4 journal checkpoint (sync + drop caches), checking again"
	"${SSH[@]}" 'sync; echo 3 > /proc/sys/vm/drop_caches; sync'
	got=$(raw_sha)
	echo "  $got"
fi
[ "$got" = "$want" ] && echo "OK: raw eMMC sectors match the image" || { echo "MISMATCH" >&2; exit 1; }
