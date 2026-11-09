# whyred shim, milestone 1

A bare-metal program that ABL starts as if it were a Linux kernel. It writes nothing to flash.
It reports the hardware on the splash screen, then reboots.

## Build

    ./build.sh        # needs aarch64-linux-gnu-gcc, mkbootimg, cpio, gzip

Output: `out/shim-boot.img` (boot image v0, gzip Image + whyred DTB appended).

## Run

    fastboot getvar all 2>&1 | grep -E "unlocked|max-download|version-bootloader"
    fastboot boot out/shim-boot.img

## What you should see

1. Dark grey screen: the shim started. A dark grey screen that never changes means reading
   the display controller registers hung.
2. White text on black with sections [cpu] [psci] [mdp] [dtb] [smp], then 2 or 3 coloured bars.
3. After 90 s the phone resets. It should land in fastboot (restart reason 0x77665500).
   If it boots Linux instead, the restart reason did not survive the reset.
4. "!! EXCEPTION" with esr/elr/far means a crash. Photograph it. Hold power + vol- to recover.

Garbled or slanted text means the probe picked the wrong stride or pixel size.
A black screen with no grey means ABL did not start the image or the splash address is wrong.

## What to photograph

- [cpu] boot core line: MIDR, EL, 16K flag, mmfr0.
- [smp] lines for cpu 100-103: the 16K flag decides whether the topaz approach works.
- [mdp] lines: the live pipe address, size, stride (st), format (f) and unpack pattern (u).
- The colour of each bar, left to right (byte 0, 1, 2 of a pixel).
