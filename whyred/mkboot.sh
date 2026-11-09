#!/bin/bash
# Build a whyred fastboot image that boots XNU.
#
#   whyred/mkboot.sh KERNEL [RAMDISK]          boot with the default boot-args below
#   whyred/mkboot.sh KERNEL none               no RAM disk (pass rd=emmcroot0 in CMDLINE)
#   CMDLINE="..." whyred/mkboot.sh KERNEL       override the boot-args
#   whyred/mkboot.sh --fetch RUN_ID             download the xnu-kernel artifact of a CI run
#
# RAMDISK defaults to whyred/out/sweet-init (tools/sweet-init.S): mockfs serves it as
# /sbin/launchd, so XNU boots to a PID 1 that prints a heartbeat. Output: whyred/out/xnu-boot.img
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/whyred/out
mkdir -p "$OUT"

if [ "${1:-}" = "--fetch" ]; then
	dir=$OUT/kernel-$2
	rm -rf "$dir"
	gh run download "$2" -R m-obeid/ios-whyred -n xnu-kernel -D "$dir"
	ls -l "$dir"
	exit 0
fi

KERNEL=${1:?usage: mkboot.sh KERNEL [RAMDISK]}
RAMDISK=${2:-$OUT/sweet-init}
# sweetcpufreq=0 sweetbattery=0: SweetBattery otherwise writes SM6225 clock and cpufreq registers.
CMDLINE=${CMDLINE:-debug=0xc serial=3 cpus=1 keepsyms=1 -noprogress -enable_kprintf_spam rd=md0 sweetcpufreq=0 sweetbattery=0}

if [ ! -f "$RAMDISK" ] && [ "$RAMDISK" = "$OUT/sweet-init" ]; then
	clang -target arm64-apple-ios16.0 -c "$ROOT/tools/sweet-init.S" -o "$OUT/sweet-init.o"
	python3 "$ROOT/tools/mkinit.py" "$OUT/sweet-init.o" "$OUT/sweet-init"
fi

python3 "$ROOT/whyred/tools/mkadt_whyred.py" "$OUT/whyred.adt"
rdargs=()
[ "$RAMDISK" = none ] || rdargs=(--ramdisk "$RAMDISK")
python3 "$ROOT/tools/mkpayload.py" "$KERNEL" "$OUT/whyred.adt" "$OUT/payload.bin" \
	--cmdline "$CMDLINE" "${rdargs[@]}"
"$ROOT/whyred/shim/build.sh" "$OUT/payload.bin"
cp "$ROOT/whyred/shim/out/shim-boot.img" "$OUT/xnu-boot.img"
echo "boot-args: $CMDLINE"
echo "fastboot boot $OUT/xnu-boot.img"
