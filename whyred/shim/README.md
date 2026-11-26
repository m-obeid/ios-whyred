# whyred shim

A bare-metal program that ABL starts as if it were a Linux kernel. It writes nothing to flash.
With a payload it boots XNU. Without one it reports the hardware on the splash screen.

## Build

    ./build.sh PAYLOAD.bin    # boot XNU (whyred/mkboot.sh calls this)
    ./build.sh                # diagnostics only

Needs aarch64-linux-gnu-gcc, mkbootimg, cpio and gzip. Output: `out/shim-boot.img`, a boot
image v0 (gzip Image + `dtb/whyred.dtb` appended), the layout of the stock whyred boot.img.

Options (environment variables, listed in `build.sh`):

| Variable | Effect |
| --- | --- |
| `GOLD_ROW=n` | Gold OSM row before XNU: 4 = 1804.8 MHz (default), 0 = leave Gold at 600 MHz |
| `CPRH_OL=1` | CPRH open loop (fused ceiling voltages) instead of the closed loop |
| `OSM=1` | Read-only Gold clock and voltage probe. `OSM_TARGET=n` also requests row n |
| `EMMC=1` | Read-only eMMC probe |
| `MSS=1` | Modem MBA boot probe |
| `SCALE=n` | Console text scale (3 reads well through a webcam) |

## What it does on an XNU boot

1. On Silver CPU0: wake Gold `0x100` with PSCI `CPU_ON`. Silver cores lack the 16K granule.
2. On Gold: GIC setup for all Gold cores, OSM and CPRH bring-up (`osm.c`), then the jump into
   XNU with the ADT and boot-args from the payload (`xnu.c`).
3. Silver CPU0 calls `CPU_OFF` 1 s after the hand-off when `MIGRATE_INFO_TYPE` is 2, else parks
   in `wfi`. Never `CPU_OFF` the last running Gold core: the Gold cluster powers down.

An exception prints `!! EXCEPTION` with esr/elr/far and reboots after 60 s, which lands in Linux.
Hold power + volume down for fastboot.
